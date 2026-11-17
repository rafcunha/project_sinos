/**
 * @file wioe5.c
 * @brief Wio-E5 driver: command engine (L3) and operations (L4).
 *
 * See firmware/node_device/docs/wioe5/driver_spec.md, sections 8, 10, 11 and 17.
 *
 * Execution model
 * ---------------
 * An operation (setup, join, send, ...) is a small state machine whose step
 * function is called each time the previous *action* finishes. An action is
 * either an AT command (sent after the RX guard time, ended by a terminal line
 * or a timeout) or a plain delay. wioe5_process() drains received bytes into
 * lines, routes each line to the command in flight or to the URC handler, and
 * then advances the current action.
 */
#include "wioe5.h"

#include <string.h>

#include "wioe5_hex.h"
#include "wioe5_parse.h"

#if (WIOE5_RX_RING_SIZE & (WIOE5_RX_RING_SIZE - 1u)) != 0u
#error "WIOE5_RX_RING_SIZE must be a power of two"
#endif

/* ------------------------------------------------------------------------ */
/* Private enums                                                            */
/* ------------------------------------------------------------------------ */

enum { ST_UNINIT = 0, ST_IDLE, ST_SLEEPING };

enum { ACT_NONE = 0, ACT_START, ACT_PENDING, ACT_CMD, ACT_WAIT, ACT_DONE };

enum {
    C_NONE = 0,
    C_PROBE,
    C_LOG,
    C_VER,
    C_DEVEUI,
    C_MODE,
    C_BAND,
    C_CH,
    C_ADR,
    C_DR,
    C_PORT,
    C_RETRY,
    C_REPT,
    C_APPEUI,
    C_APPKEY,
    C_LEN,
    C_JOIN,
    C_MSG,
    C_LOWPOWER,
    C_RESET,
    C_COUNT
};

/** Response name expected for each command (C_MSG accepts the whole family). */
static const char *const CMD_RESP_NAME[C_COUNT] = {
    [C_NONE] = "",        [C_PROBE] = "AT",    [C_LOG] = "LOG",           [C_VER] = "VER",
    [C_DEVEUI] = "ID",    [C_MODE] = "MODE",   [C_BAND] = "DR",           [C_CH] = "CH",
    [C_ADR] = "ADR",      [C_DR] = "DR",       [C_PORT] = "PORT",         [C_RETRY] = "RETRY",
    [C_REPT] = "REPT",    [C_APPEUI] = "ID",   [C_APPKEY] = "KEY",        [C_LEN] = "LW",
    [C_JOIN] = "JOIN",    [C_MSG] = "MSGHEX",  [C_LOWPOWER] = "LOWPOWER", [C_RESET] = "RESET",
};

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static uint32_t now_ms(const wioe5_t *d)
{
    return d->port.now_ms(d->port.ctx);
}

static char up_char(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - ('a' - 'A')) : c;
}

static bool ieq(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        if (up_char(*a) != up_char(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0') && (*b == '\0');
}

static bool istarts(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if ((*s == '\0') || (up_char(*s) != up_char(*prefix))) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

/** Transmission time added to a command timeout: ceil(len * 1.1) ms. */
static uint32_t tx_time_ms(size_t len)
{
    return (uint32_t)((len * 11u + 9u) / 10u);
}

static void emit(wioe5_t *d, wioe5_event_t evt)
{
    if (d->cfg.on_event != NULL) {
        d->cfg.on_event(d->cfg.user, evt);
    }
}

/* ---- Logging (never logs key material) ----------------------------------- */

static void log_join(wioe5_t *d, wioe5_log_level_t lvl, const char *a, const char *b, size_t blen)
{
    char   buf[96];
    size_t n = 0u;
    if (d->port.log == NULL) {
        return;
    }
    while ((*a != '\0') && (n < sizeof(buf) - 1u)) {
        buf[n++] = *a++;
    }
    for (size_t i = 0u; (b != NULL) && (i < blen) && (n < sizeof(buf) - 1u); i++) {
        char c   = b[i];
        buf[n++] = ((c >= 0x20) && (c < 0x7F)) ? c : '.';
    }
    buf[n] = '\0';
    d->port.log(d->port.ctx, lvl, buf);
}

static void log_tx(wioe5_t *d)
{
    if (d->cur_cmd == C_APPKEY) {
        log_join(d, WIOE5_LOG_DEBUG, "TX AT+KEY=APPKEY,<redacted>", NULL, 0u);
        return;
    }
    size_t n = d->tx_len;
    while ((n > 0u) && ((d->tx_buf[n - 1u] == '\n') || (d->tx_buf[n - 1u] == '\r'))) {
        n--;
    }
    log_join(d, WIOE5_LOG_DEBUG, "TX ", d->tx_buf, n);
}

static const char *status_name(wioe5_status_t st)
{
    static const char *const names[] = {
        "OK",        "ERR_PARAM",      "ERR_BUSY",       "ERR_STATE",       "ERR_UART",
        "ERR_TIMEOUT", "ERR_AT",       "ERR_UNEXPECTED", "ERR_OVERFLOW",    "ERR_MODEM_BUSY",
        "ERR_NOT_JOINED", "ERR_JOIN_FAILED", "ERR_NO_FREE_CH", "ERR_NO_BAND", "ERR_DR",
        "ERR_LENGTH", "ERR_NO_ACK",
    };
    size_t i = (size_t)st;
    return (i < sizeof(names) / sizeof(names[0])) ? names[i] : "?";
}

/* ---- TX buffer builder ---------------------------------------------------- */

static void tx_begin(wioe5_t *d)
{
    d->tx_len    = 0u;
    d->tx_buf[0] = '\0';
}

static bool tx_str(wioe5_t *d, const char *s)
{
    size_t n = strlen(s);
    if (d->tx_len + n >= WIOE5_TX_BUF_SIZE) {
        return false;
    }
    memcpy(&d->tx_buf[d->tx_len], s, n + 1u);
    d->tx_len += n;
    return true;
}

static bool tx_uint(wioe5_t *d, uint32_t v)
{
    char   tmp[11];
    size_t i = sizeof(tmp) - 1u;
    tmp[i]   = '\0';
    do {
        tmp[--i] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while ((v != 0u) && (i > 0u));
    return tx_str(d, &tmp[i]);
}

static bool tx_hex(wioe5_t *d, const uint8_t *data, size_t n)
{
    if (!wioe5_hex_encode(data, n, &d->tx_buf[d->tx_len], WIOE5_TX_BUF_SIZE - d->tx_len)) {
        return false;
    }
    d->tx_len += 2u * n;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                  */
/* ------------------------------------------------------------------------ */

static void finish(wioe5_t *d, wioe5_status_t st)
{
    wioe5_op_t op = (wioe5_op_t)d->op;
    d->op         = (uint8_t)WIOE5_OP_NONE;
    d->act        = ACT_NONE;
    d->cur_cmd    = C_NONE;
    d->result     = st;
    if (st != WIOE5_OK) {
        log_join(d, WIOE5_LOG_WARN, "op failed: ", status_name(st), strlen(status_name(st)));
    }
    if (d->cfg.on_op_done != NULL) {
        d->cfg.on_op_done(d->cfg.user, op, st);
    }
}

/** Queue the command in tx_buf (must end with "\r\n"). */
static void issue(wioe5_t *d, uint8_t cmd, uint32_t base_timeout)
{
    d->cur_cmd     = cmd;
    d->cmd_timeout = base_timeout;
    d->act         = ACT_PENDING;
}

/** Build "AT<suffix>\r\n" and queue it. */
static void issue_simple(wioe5_t *d, uint8_t cmd, const char *text, uint32_t timeout)
{
    tx_begin(d);
    (void)tx_str(d, text);
    (void)tx_str(d, "\r\n");
    issue(d, cmd, timeout);
}

/** Build "<prefix><n>\r\n", remember n as the expected echo, and queue it. */
static void issue_uint(wioe5_t *d, uint8_t cmd, const char *prefix, uint32_t n)
{
    tx_begin(d);
    (void)tx_str(d, prefix);
    (void)tx_uint(d, n);
    (void)tx_str(d, "\r\n");
    d->cmd_arg = n;
    issue(d, cmd, d->cfg.timeouts.cmd_eeprom_ms);
}

static void wait_ms(wioe5_t *d, uint32_t ms)
{
    d->act     = ACT_WAIT;
    d->t_mark  = now_ms(d);
    d->t_limit = ms;
}

static void complete(wioe5_t *d, wioe5_status_t st)
{
    d->act        = ACT_DONE;
    d->act_status = st;
}

/** Send the pending command if the RX line has been quiet long enough.
 *  @return false if it must wait. */
static bool send_pending(wioe5_t *d, uint32_t now)
{
    if (d->rx_seen && ((uint32_t)(now - d->last_rx_ms) < d->cfg.timeouts.guard_ms)) {
        return false;
    }
    if (d->port.write(d->port.ctx, (const uint8_t *)d->tx_buf, d->tx_len) != 0) {
        finish(d, WIOE5_ERR_UART);
        return true;
    }
    log_tx(d);
    d->act         = ACT_CMD;
    d->t_mark      = now;
    d->t_limit     = d->cmd_timeout + tx_time_ms(d->tx_len);
    d->acked       = false;
    d->netid_seen  = false;
    d->join_failed = false;
    d->dl_pending  = false;
    d->dl_rssi     = INT16_MIN;
    d->dl_snr      = 0;
    d->dl_window   = 0xFFu;
    return true;
}

static void on_timeout(wioe5_t *d)
{
    if ((d->cur_cmd == C_JOIN) && d->join_failed) {
        complete(d, WIOE5_ERR_JOIN_FAILED);
        return;
    }
    d->info.consecutive_timeouts++;
    if ((d->info.consecutive_timeouts >= WIOE5_UNRESPONSIVE_THRESHOLD) &&
        !d->unresponsive_reported) {
        d->unresponsive_reported = true;
        emit(d, WIOE5_EVT_UNRESPONSIVE);
    }
    complete(d, WIOE5_ERR_TIMEOUT);
}

/* ------------------------------------------------------------------------ */
/* Response handling                                                        */
/* ------------------------------------------------------------------------ */

static bool is_msg_family(const wioe5_resp_t *r)
{
    return wioe5_resp_name_is(r, "MSG") || wioe5_resp_name_is(r, "MSGHEX") ||
           wioe5_resp_name_is(r, "CMSG") || wioe5_resp_name_is(r, "CMSGHEX");
}

static bool belongs_to_cmd(const wioe5_t *d, const wioe5_resp_t *r)
{
    if ((d->act != ACT_CMD) || (d->cur_cmd == C_NONE) || (d->cur_cmd >= C_COUNT)) {
        return false;
    }
    if (d->cur_cmd == C_MSG) {
        return is_msg_family(r);
    }
    if ((d->cur_cmd == C_LOWPOWER) && ieq(r->body, "WAKEUP")) {
        return false; /* always a URC */
    }
    return wioe5_resp_name_is(r, CMD_RESP_NAME[d->cur_cmd]);
}

/** Map the error events shared by JOIN and MSG; WIOE5_OK if not an error. */
static wioe5_status_t map_event_error(wioe5_t *d, const wioe5_ev_t *ev)
{
    switch (ev->kind) {
    case WIOE5_EV_ERR_BUSY:
        return WIOE5_ERR_MODEM_BUSY;
    case WIOE5_EV_ERR_NOT_JOINED:
        d->info.joined = false;
        return WIOE5_ERR_NOT_JOINED;
    case WIOE5_EV_ERR_NO_FREE_CH:
        return WIOE5_ERR_NO_FREE_CH;
    case WIOE5_EV_ERR_NO_BAND:
        d->info.retry_after_ms = ev->value;
        return WIOE5_ERR_NO_BAND;
    case WIOE5_EV_ERR_DR:
        return WIOE5_ERR_DR;
    case WIOE5_EV_ERR_LENGTH:
        return WIOE5_ERR_LENGTH;
    default:
        return WIOE5_OK;
    }
}

static void handle_simple(wioe5_t *d, const wioe5_resp_t *r)
{
    const char    *b  = r->body;
    wioe5_status_t st = WIOE5_ERR_UNEXPECTED;
    uint32_t       v  = 0u;
    uint8_t        tmp[8];

    switch (d->cur_cmd) {
    case C_PROBE:
    case C_RESET:
        st = ieq(b, "OK") ? WIOE5_OK : WIOE5_ERR_UNEXPECTED;
        break;
    case C_LOG:
    case C_BAND:
    case C_CH:
    case C_DR:
        st = WIOE5_OK; /* format not confirmed on the bench yet (spec 17.2) */
        break;
    case C_VER:
        if (wioe5_parse_ver(b, d->info.fw_version, sizeof(d->info.fw_version))) {
            st = WIOE5_OK;
        }
        break;
    case C_DEVEUI:
        if (wioe5_parse_id(b, "DevEui", d->info.dev_eui, 8u)) {
            st = WIOE5_OK;
        }
        break;
    case C_MODE:
        st = ieq(b, "LWOTAA") ? WIOE5_OK : WIOE5_ERR_UNEXPECTED;
        break;
    case C_ADR:
        st = ieq(b, d->cfg.adr ? "ON" : "OFF") ? WIOE5_OK : WIOE5_ERR_UNEXPECTED;
        break;
    case C_PORT:
    case C_RETRY:
    case C_REPT:
        if (wioe5_parse_uint_body(b, &v) && (v == d->cmd_arg)) {
            st = WIOE5_OK;
        }
        break;
    case C_APPEUI:
        if ((d->cfg.provision != NULL) && wioe5_parse_id(b, "AppEui", tmp, 8u) &&
            (memcmp(tmp, d->cfg.provision->app_eui, 8u) == 0)) {
            st = WIOE5_OK;
        }
        break;
    case C_APPKEY:
        st = istarts(b, "APPKEY") ? WIOE5_OK : WIOE5_ERR_UNEXPECTED;
        break;
    case C_LEN:
        if (wioe5_parse_lw_len(b, &d->info.max_payload)) {
            st = WIOE5_OK;
        }
        break;
    case C_LOWPOWER:
        st = ieq(b, "SLEEP") ? WIOE5_OK : WIOE5_ERR_UNEXPECTED;
        break;
    default:
        break;
    }
    complete(d, st);
}

static void handle_join(wioe5_t *d, const wioe5_resp_t *r)
{
    wioe5_ev_t ev;
    (void)wioe5_parse_event(r->body, &ev);
    wioe5_status_t err = map_event_error(d, &ev);
    if (err != WIOE5_OK) {
        complete(d, err);
        return;
    }
    switch (ev.kind) {
    case WIOE5_EV_NETID:
        d->netid_seen = true;
        memcpy(d->info.dev_addr, ev.dev_addr, sizeof(d->info.dev_addr));
        d->info.net_id = ev.net_id;
        break;
    case WIOE5_EV_JOINED_ALREADY:
        complete(d, WIOE5_OK);
        break;
    case WIOE5_EV_JOIN_FAILED:
        d->join_failed = true;
        d->t_mark      = now_ms(d);
        d->t_limit     = d->cfg.timeouts.join_fail_grace_ms;
        break;
    case WIOE5_EV_DONE:
        if (d->join_failed) {
            complete(d, WIOE5_ERR_JOIN_FAILED);
        } else if (d->netid_seen) {
            complete(d, WIOE5_OK);
        } else {
            complete(d, WIOE5_ERR_UNEXPECTED);
        }
        break;
    default:
        break; /* Start, NORMAL, unknown lines: keep waiting */
    }
}

static void deliver_downlink(wioe5_t *d)
{
    if (!d->dl_pending) {
        return;
    }
    d->dl_pending = false;
    if (d->cfg.on_downlink != NULL) {
        wioe5_downlink_t dl;
        dl.port      = d->dl_port;
        dl.data      = d->dl_buf;
        dl.len       = d->dl_len;
        dl.rssi_dbm  = d->dl_rssi;
        dl.snr_cdb   = d->dl_snr;
        dl.rx_window = d->dl_window;
        d->cfg.on_downlink(d->cfg.user, &dl);
    }
}

static void handle_msg(wioe5_t *d, const wioe5_resp_t *r)
{
    wioe5_ev_t ev;
    (void)wioe5_parse_event(r->body, &ev);
    wioe5_status_t err = map_event_error(d, &ev);
    if (err != WIOE5_OK) {
        complete(d, err);
        return;
    }
    switch (ev.kind) {
    case WIOE5_EV_ACK:
        d->acked = true;
        break;
    case WIOE5_EV_LINK:
        d->info.link_margin_db = ev.link_margin;
        d->info.link_gw_count  = ev.link_gw;
        break;
    case WIOE5_EV_DOWNLINK: {
        size_t n = 0u;
        if (wioe5_hex_decode(ev.hex, ev.hex_len, d->dl_buf, sizeof(d->dl_buf), &n)) {
            d->dl_pending = true;
            d->dl_port    = ev.port;
            d->dl_len     = n;
        } else {
            emit(d, WIOE5_EVT_BAD_DOWNLINK);
        }
        break;
    }
    case WIOE5_EV_RXINFO:
        d->info.last_rssi_dbm = ev.rssi_dbm;
        d->info.last_snr_cdb  = ev.snr_cdb;
        d->dl_rssi            = ev.rssi_dbm;
        d->dl_snr             = ev.snr_cdb;
        d->dl_window          = ev.rx_window;
        break;
    case WIOE5_EV_DONE:
        deliver_downlink(d);
        if (d->msg_confirmed && !d->msg_flush && !d->acked) {
            complete(d, WIOE5_ERR_NO_ACK);
        } else {
            complete(d, WIOE5_OK);
        }
        break;
    default:
        break; /* Start, Wait ACK, TX, FPENDING, MULTICAST, unknown */
    }
}

static void handle_urc(wioe5_t *d, const wioe5_resp_t *r)
{
    if (wioe5_resp_name_is(r, "LOWPOWER") && ieq(r->body, "WAKEUP")) {
        d->info.sleeping = false;
        if (((wioe5_op_t)d->op == WIOE5_OP_NONE) && (d->state == ST_SLEEPING)) {
            d->state = ST_IDLE;
        }
        emit(d, WIOE5_EVT_WOKE);
        return;
    }
    if (wioe5_resp_name_is(r, "INFO") && istarts(r->body, "Input timeout")) {
        emit(d, WIOE5_EVT_INPUT_TIMEOUT);
        return;
    }
    if (wioe5_resp_name_is(r, "JOIN")) {
        wioe5_ev_t ev;
        if (wioe5_parse_event(r->body, &ev) && (ev.kind == WIOE5_EV_NETID)) {
            memcpy(d->info.dev_addr, ev.dev_addr, sizeof(d->info.dev_addr));
            d->info.net_id = ev.net_id;
            d->info.joined = true;
            emit(d, WIOE5_EVT_JOINED);
        }
        return;
    }
    d->info.unknown_lines++;
    if (wioe5_resp_name_is(r, "KEY")) {
        log_join(d, WIOE5_LOG_DEBUG, "RX unhandled +KEY line", NULL, 0u);
    } else {
        log_join(d, WIOE5_LOG_DEBUG, "RX unhandled +", r->name, strlen(r->name));
    }
}

static void handle_line(wioe5_t *d, char *line)
{
    wioe5_resp_t r;
    if (!wioe5_parse_resp(line, &r)) {
        d->info.unknown_lines++;
        return;
    }
    if (!belongs_to_cmd(d, &r)) {
        handle_urc(d, &r);
        return;
    }
    d->info.consecutive_timeouts = 0u;
    d->unresponsive_reported     = false;
    if (r.is_error) {
        d->info.last_at_error = r.error_code;
        complete(d, WIOE5_ERR_AT);
        return;
    }
    if (d->cur_cmd == C_JOIN) {
        handle_join(d, &r);
    } else if (d->cur_cmd == C_MSG) {
        handle_msg(d, &r);
    } else {
        handle_simple(d, &r);
    }
}

static void abort_op(wioe5_t *d, wioe5_status_t st)
{
    if ((wioe5_op_t)d->op != WIOE5_OP_NONE) {
        finish(d, st);
    }
}

static void drain_rx(wioe5_t *d)
{
    uint8_t  b;
    uint32_t now = now_ms(d);
    while (wioe5_rb_pop(&d->rb, &b)) {
        char    *line   = NULL;
        uint32_t before = d->la.unknown_lines;
        d->rx_seen      = true;
        d->last_rx_ms   = now;
        wioe5_line_result_t res = wioe5_lineasm_feed(&d->la, b, &line);
        if (d->la.unknown_lines != before) {
            d->info.unknown_lines++;
        }
        if (res == WIOE5_LINE_READY) {
            handle_line(d, line);
        } else if (res == WIOE5_LINE_OVERFLOW) {
            d->info.line_overflows++;
            emit(d, WIOE5_EVT_LINE_OVERFLOW);
            abort_op(d, WIOE5_ERR_OVERFLOW);
        } else {
            /* byte consumed */
        }
    }
    uint32_t ovf = d->rb.overflows;
    if (ovf != d->seen_rx_overflows) {
        d->seen_rx_overflows = ovf;
        d->info.rx_overflows = ovf;
        emit(d, WIOE5_EVT_RX_OVERFLOW);
        abort_op(d, WIOE5_ERR_OVERFLOW);
    }
}

/* ------------------------------------------------------------------------ */
/* Operations                                                               */
/* ------------------------------------------------------------------------ */

#define CHECK(st)                                                                              \
    do {                                                                                       \
        if ((st) != WIOE5_OK) {                                                                \
            finish(d, (st));                                                                   \
            return;                                                                            \
        }                                                                                      \
    } while (0)

static void set_reset_pin(wioe5_t *d, bool asserted)
{
    if (d->port.set_reset != NULL) {
        d->port.set_reset(d->port.ctx, asserted);
    }
}

static void mark_reset(wioe5_t *d)
{
    d->state             = ST_UNINIT;
    d->setup_done        = false;
    d->module_port_known = false;
    d->info.joined       = false;
    d->info.sleeping     = false;
}

/**
 * Embedded wake sequence: wake byte, settle delay, AT probe (2 tries).
 * @return 0 while in progress, 1 when awake, -1 on failure (*err set).
 */
static int wake_seq(wioe5_t *d, wioe5_status_t st, wioe5_status_t *err)
{
    switch (d->wphase) {
    case 0:
        d->tx_buf[0] = (char)d->cfg.wake_byte;
        d->tx_len    = 1u;
        if (d->port.write(d->port.ctx, (const uint8_t *)d->tx_buf, 1u) != 0) {
            *err = WIOE5_ERR_UART;
            return -1;
        }
        log_join(d, WIOE5_LOG_DEBUG, "TX <wake byte>", NULL, 0u);
        wait_ms(d, d->cfg.timeouts.wake_settle_ms);
        d->wphase = 1u;
        return 0;
    case 1:
        d->wattempts = 0u;
        issue_simple(d, C_PROBE, "AT", d->cfg.timeouts.cmd_ms);
        d->wphase = 2u;
        return 0;
    default:
        if (st == WIOE5_OK) {
            d->state         = ST_IDLE;
            d->info.sleeping = false;
            return 1;
        }
        d->wattempts++;
        if (d->wattempts < 2u) {
            issue_simple(d, C_PROBE, "AT", d->cfg.timeouts.cmd_ms);
            return 0;
        }
        d->info.sleeping = true;
        *err             = st;
        return -1;
    }
}

static void step_setup(wioe5_t *d, wioe5_status_t st)
{
    const wioe5_timeouts_t *t = &d->cfg.timeouts;
    for (;;) {
        switch (d->phase) {
        case 0:
            if (d->port.set_reset != NULL) {
                set_reset_pin(d, true);
                wait_ms(d, t->reset_pulse_ms);
                d->phase = 1u;
                return;
            }
            d->phase = 3u;
            break;
        case 1:
            set_reset_pin(d, false);
            wait_ms(d, t->boot_ms);
            d->phase = 2u;
            return;
        case 2:
            wioe5_lineasm_reset(&d->la); /* drop any partial boot line */
            d->phase = 3u;
            break;
        case 3:
            d->attempts = 0u;
            issue_simple(d, C_PROBE, "AT", t->cmd_ms);
            d->phase = 4u;
            return;
        case 4:
            if (st != WIOE5_OK) {
                d->attempts++;
                if (d->attempts < 3u) {
                    issue_simple(d, C_PROBE, "AT", t->cmd_ms);
                    return;
                }
                finish(d, st);
                return;
            }
            issue_simple(d, C_LOG, "AT+LOG=QUIET", t->cmd_ms);
            d->phase = 5u;
            return;
        case 5: /* AT+LOG is optional: ignore its result */
            issue_simple(d, C_VER, "AT+VER", t->cmd_ms);
            d->phase = 6u;
            return;
        case 6:
            CHECK(st);
            issue_simple(d, C_DEVEUI, "AT+ID=DevEui", t->cmd_ms);
            d->phase = 7u;
            return;
        case 7:
            CHECK(st);
            issue_simple(d, C_MODE, "AT+MODE=LWOTAA", t->cmd_eeprom_ms);
            d->phase = 8u;
            return;
        case 8:
            CHECK(st);
            tx_begin(d);
            (void)tx_str(d, "AT+DR=");
            (void)tx_str(d, d->cfg.band);
            (void)tx_str(d, "\r\n");
            issue(d, C_BAND, t->cmd_eeprom_ms);
            d->phase = 9u;
            return;
        case 9:
            CHECK(st);
            d->phase = 10u;
            if (d->cfg.channel_mask != NULL) {
                tx_begin(d);
                (void)tx_str(d, "AT+CH=NUM,");
                (void)tx_str(d, d->cfg.channel_mask);
                (void)tx_str(d, "\r\n");
                issue(d, C_CH, t->cmd_eeprom_ms);
                return;
            }
            break;
        case 10:
            CHECK(st);
            issue_simple(d, C_ADR, d->cfg.adr ? "AT+ADR=ON" : "AT+ADR=OFF", t->cmd_eeprom_ms);
            d->phase = 11u;
            return;
        case 11:
            CHECK(st);
            d->phase = 12u;
            if (!d->cfg.adr) {
                tx_begin(d);
                (void)tx_str(d, "AT+DR=");
                (void)tx_uint(d, d->cfg.data_rate);
                (void)tx_str(d, "\r\n");
                issue(d, C_DR, t->cmd_eeprom_ms);
                return;
            }
            break;
        case 12:
            CHECK(st);
            issue_uint(d, C_PORT, "AT+PORT=", d->cfg.default_port);
            d->phase = 13u;
            return;
        case 13:
            CHECK(st);
            d->module_port       = d->cfg.default_port;
            d->module_port_known = true;
            issue_uint(d, C_RETRY, "AT+RETRY=", d->cfg.retry);
            d->phase = 14u;
            return;
        case 14:
            CHECK(st);
            issue_uint(d, C_REPT, "AT+REPT=", d->cfg.rept);
            d->phase = 15u;
            return;
        case 15:
            CHECK(st);
            d->phase = 17u;
            if (d->cfg.provision != NULL) {
                tx_begin(d);
                (void)tx_str(d, "AT+ID=AppEui,\"");
                (void)tx_hex(d, d->cfg.provision->app_eui, 8u);
                (void)tx_str(d, "\"\r\n");
                issue(d, C_APPEUI, t->cmd_eeprom_ms);
                d->phase = 16u;
                return;
            }
            break;
        case 16:
            CHECK(st);
            tx_begin(d);
            (void)tx_str(d, "AT+KEY=APPKEY,\"");
            (void)tx_hex(d, d->cfg.provision->app_key, 16u);
            (void)tx_str(d, "\"\r\n");
            issue(d, C_APPKEY, t->cmd_eeprom_ms);
            d->phase = 17u;
            return;
        default:
            CHECK(st);
            d->setup_done    = true;
            d->state         = ST_IDLE;
            d->info.sleeping = false;
            finish(d, WIOE5_OK);
            return;
        }
        st = WIOE5_OK; /* phases skipped via 'break' carry no result */
    }
}

static void step_join(wioe5_t *d, wioe5_status_t st)
{
    wioe5_status_t err = WIOE5_OK;
    for (;;) {
        switch (d->phase) {
        case 0:
            d->phase = (d->state == ST_SLEEPING) ? 1u : 2u;
            break;
        case 1: {
            int w = wake_seq(d, st, &err);
            if (w == 0) {
                return;
            }
            if (w < 0) {
                finish(d, err);
                return;
            }
            d->phase = 2u;
            break;
        }
        case 2:
            issue_simple(d, C_JOIN, d->force_join ? "AT+JOIN=FORCE" : "AT+JOIN",
                         d->cfg.timeouts.join_ms);
            d->phase = 3u;
            return;
        default:
            if (st == WIOE5_OK) {
                d->info.joined = true;
            }
            finish(d, st);
            return;
        }
        st = WIOE5_OK;
    }
}

static uint32_t msg_timeout(const wioe5_t *d, bool confirmed)
{
    const wioe5_timeouts_t *t = &d->cfg.timeouts;
    if (confirmed) {
        uint32_t n = (d->cfg.retry < 1u) ? 1u : d->cfg.retry;
        return t->cmsg_ms * n + t->cmsg_gap_ms * (n - 1u);
    }
    return t->msg_ms * d->cfg.rept;
}

static void step_send(wioe5_t *d, wioe5_status_t st)
{
    wioe5_status_t err = WIOE5_OK;
    for (;;) {
        switch (d->phase) {
        case 0:
            d->phase = (d->state == ST_SLEEPING) ? 1u : 2u;
            break;
        case 1: {
            int w = wake_seq(d, st, &err);
            if (w == 0) {
                return;
            }
            if (w < 0) {
                finish(d, err);
                return;
            }
            d->phase = 2u;
            break;
        }
        case 2:
            d->phase = 4u;
            if (!d->module_port_known || (d->module_port != d->payload_port)) {
                issue_uint(d, C_PORT, "AT+PORT=", d->payload_port);
                d->phase = 3u;
                return;
            }
            break;
        case 3:
            CHECK(st);
            d->module_port       = d->payload_port;
            d->module_port_known = true;
            d->phase             = 4u;
            break;
        case 4:
            d->phase = 7u;
            if (d->cfg.check_len_before_send && (d->payload_len > 0u)) {
                d->attempts = 0u;
                issue_simple(d, C_LEN, "AT+LW=LEN", d->cfg.timeouts.cmd_ms);
                d->phase = 5u;
                return;
            }
            break;
        case 5:
            CHECK(st);
            if (d->info.max_payload == 0u) {
                if (d->attempts == 0u) {
                    /* Pending MAC command: flush it with an empty uplink (PDF 4.5.2). */
                    d->attempts      = 1u;
                    d->msg_flush     = true;
                    d->msg_confirmed = false;
                    issue_simple(d, C_MSG, "AT+MSGHEX", msg_timeout(d, false));
                    d->phase = 6u;
                    return;
                }
                finish(d, WIOE5_ERR_LENGTH);
                return;
            }
            if (d->payload_len > d->info.max_payload) {
                finish(d, WIOE5_ERR_LENGTH);
                return;
            }
            d->phase = 7u;
            break;
        case 6:
            CHECK(st);
            issue_simple(d, C_LEN, "AT+LW=LEN", d->cfg.timeouts.cmd_ms);
            d->phase = 5u;
            return;
        case 7:
            tx_begin(d);
            (void)tx_str(d, d->payload_confirmed ? "AT+CMSGHEX" : "AT+MSGHEX");
            if (d->payload_len > 0u) {
                (void)tx_str(d, "=\"");
                (void)tx_hex(d, d->payload, d->payload_len);
                (void)tx_str(d, "\"");
            }
            if (!tx_str(d, "\r\n")) {
                finish(d, WIOE5_ERR_LENGTH);
                return;
            }
            d->msg_flush     = false;
            d->msg_confirmed = d->payload_confirmed;
            issue(d, C_MSG, msg_timeout(d, d->payload_confirmed));
            d->phase = 8u;
            return;
        default:
            finish(d, st);
            return;
        }
        st = WIOE5_OK;
    }
}

static void step_sleep(wioe5_t *d, wioe5_status_t st)
{
    if (d->phase == 0u) {
        issue_simple(d, C_LOWPOWER, "AT+LOWPOWER", d->cfg.timeouts.lowpower_ms);
        d->phase = 1u;
        return;
    }
    if (st == WIOE5_OK) {
        d->state         = ST_SLEEPING;
        d->info.sleeping = true;
    }
    finish(d, st);
}

static void step_wake(wioe5_t *d, wioe5_status_t st)
{
    wioe5_status_t err = WIOE5_OK;
    int            w   = wake_seq(d, st, &err);
    if (w == 0) {
        return;
    }
    finish(d, (w > 0) ? WIOE5_OK : err);
}

static void step_reset(wioe5_t *d, wioe5_status_t st)
{
    const wioe5_timeouts_t *t   = &d->cfg.timeouts;
    wioe5_status_t          err = WIOE5_OK;
    for (;;) {
        switch (d->phase) {
        case 0:
            if (d->port.set_reset != NULL) {
                set_reset_pin(d, true);
                wait_ms(d, t->reset_pulse_ms);
                d->phase = 1u;
                return;
            }
            d->phase = (d->state == ST_SLEEPING) ? 10u : 12u;
            break;
        case 1:
            set_reset_pin(d, false);
            wait_ms(d, t->boot_ms);
            d->phase = 2u;
            return;
        case 2:
            wioe5_lineasm_reset(&d->la);
            d->attempts = 0u;
            issue_simple(d, C_PROBE, "AT", t->cmd_ms);
            d->phase = 3u;
            return;
        case 3:
            if (st != WIOE5_OK) {
                d->attempts++;
                if (d->attempts < 3u) {
                    issue_simple(d, C_PROBE, "AT", t->cmd_ms);
                    return;
                }
            }
            mark_reset(d);
            finish(d, st);
            return;
        case 10: {
            int w = wake_seq(d, st, &err);
            if (w == 0) {
                return;
            }
            if (w < 0) {
                mark_reset(d);
                finish(d, err);
                return;
            }
            d->phase = 12u;
            break;
        }
        case 12:
            issue_simple(d, C_RESET, "AT+RESET", t->cmd_ms);
            d->phase = 13u;
            return;
        default: /* 13 */
            if (st != WIOE5_OK) {
                mark_reset(d);
                finish(d, st);
                return;
            }
            wait_ms(d, t->boot_ms);
            d->phase = 2u;
            return;
        }
        st = WIOE5_OK;
    }
}

static void step(wioe5_t *d, wioe5_status_t st)
{
    switch ((wioe5_op_t)d->op) {
    case WIOE5_OP_SETUP:
        step_setup(d, st);
        break;
    case WIOE5_OP_JOIN:
        step_join(d, st);
        break;
    case WIOE5_OP_SEND:
        step_send(d, st);
        break;
    case WIOE5_OP_SLEEP:
        step_sleep(d, st);
        break;
    case WIOE5_OP_WAKE:
        step_wake(d, st);
        break;
    case WIOE5_OP_RESET:
        step_reset(d, st);
        break;
    default:
        finish(d, WIOE5_ERR_STATE);
        break;
    }
}

static void run(wioe5_t *d)
{
    for (unsigned guard = 0u; guard < 64u; guard++) {
        if ((wioe5_op_t)d->op == WIOE5_OP_NONE) {
            return;
        }
        uint32_t now = now_ms(d);
        switch (d->act) {
        case ACT_START:
            d->act = ACT_NONE;
            step(d, WIOE5_OK);
            break;
        case ACT_PENDING:
            if (!send_pending(d, now)) {
                return;
            }
            if (d->act == ACT_CMD) {
                return;
            }
            break;
        case ACT_CMD:
            if ((uint32_t)(now - d->t_mark) < d->t_limit) {
                return;
            }
            on_timeout(d);
            break;
        case ACT_WAIT:
            if ((uint32_t)(now - d->t_mark) < d->t_limit) {
                return;
            }
            complete(d, WIOE5_OK);
            break;
        case ACT_DONE: {
            wioe5_status_t st = d->act_status;
            d->act            = ACT_NONE;
            step(d, st);
            break;
        }
        default:
            finish(d, WIOE5_ERR_UNEXPECTED); /* step left no action: internal error */
            return;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void wioe5_config_default(wioe5_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->band                        = "AU915";
    cfg->channel_mask                = "8-15,65";
    cfg->adr                         = true;
    cfg->data_rate                   = 2u;
    cfg->default_port                = 1u;
    cfg->retry                       = 3u;
    cfg->rept                        = 1u;
    cfg->check_len_before_send       = true;
    cfg->provision                   = NULL;
    cfg->wake_byte                   = 0xFFu;
    cfg->timeouts.guard_ms           = 10u;
    cfg->timeouts.cmd_ms             = 1000u;
    cfg->timeouts.cmd_eeprom_ms      = 2000u;
    cfg->timeouts.join_ms            = 20000u;
    cfg->timeouts.join_fail_grace_ms = 2000u;
    cfg->timeouts.msg_ms             = 15000u;
    cfg->timeouts.cmsg_ms            = 15000u;
    cfg->timeouts.cmsg_gap_ms        = 10000u;
    cfg->timeouts.lowpower_ms        = 1000u;
    cfg->timeouts.wake_settle_ms     = 20u;
    cfg->timeouts.reset_pulse_ms     = 10u;
    cfg->timeouts.boot_ms            = 2000u;
}

wioe5_status_t wioe5_init(wioe5_t *dev, const wioe5_port_t *port, const wioe5_config_t *cfg)
{
    if ((dev == NULL) || (port == NULL) || (cfg == NULL) || (port->write == NULL) ||
        (port->now_ms == NULL) || (cfg->band == NULL) || (cfg->default_port == 0u) ||
        (cfg->default_port > 223u) || (cfg->rept == 0u)) {
        return WIOE5_ERR_PARAM;
    }
    memset(dev, 0, sizeof(*dev));
    dev->port = *port;
    dev->cfg  = *cfg;
    (void)wioe5_rb_init(&dev->rb, dev->rb_storage, WIOE5_RX_RING_SIZE);
    wioe5_lineasm_init(&dev->la, dev->line_storage, WIOE5_LINE_MAX);
    dev->state              = ST_UNINIT;
    dev->op                 = (uint8_t)WIOE5_OP_NONE;
    dev->act                = ACT_NONE;
    dev->result             = WIOE5_OK;
    dev->info.last_rssi_dbm = INT16_MIN;
    return WIOE5_OK;
}

void wioe5_rx_isr(wioe5_t *dev, uint8_t byte)
{
    if ((dev != NULL) && (dev->rb.buf != NULL)) {
        (void)wioe5_rb_push(&dev->rb, byte);
    }
}

void wioe5_process(wioe5_t *dev)
{
    if ((dev == NULL) || (dev->port.write == NULL)) {
        return;
    }
    drain_rx(dev);
    run(dev);
}

static wioe5_status_t check_can_start(const wioe5_t *dev)
{
    if (dev == NULL) {
        return WIOE5_ERR_PARAM;
    }
    if (dev->port.write == NULL) {
        return WIOE5_ERR_STATE;
    }
    if ((wioe5_op_t)dev->op != WIOE5_OP_NONE) {
        return WIOE5_ERR_BUSY;
    }
    return WIOE5_OK;
}

static wioe5_status_t begin(wioe5_t *dev, wioe5_op_t op)
{
    dev->op        = (uint8_t)op;
    dev->phase     = 0u;
    dev->wphase    = 0u;
    dev->attempts  = 0u;
    dev->wattempts = 0u;
    dev->act       = ACT_START;
    return WIOE5_OK;
}

wioe5_status_t wioe5_start_setup(wioe5_t *dev)
{
    wioe5_status_t st = check_can_start(dev);
    if (st != WIOE5_OK) {
        return st;
    }
    mark_reset(dev); /* MODE resets the LoRaWAN stack; failure leaves UNINIT */
    return begin(dev, WIOE5_OP_SETUP);
}

wioe5_status_t wioe5_start_join(wioe5_t *dev, bool force)
{
    wioe5_status_t st = check_can_start(dev);
    if (st != WIOE5_OK) {
        return st;
    }
    if (!dev->setup_done || (dev->state == ST_UNINIT)) {
        return WIOE5_ERR_STATE;
    }
    dev->force_join = force;
    return begin(dev, WIOE5_OP_JOIN);
}

wioe5_status_t wioe5_start_send(wioe5_t *dev, uint8_t fport, const uint8_t *data, size_t len,
                                bool confirmed)
{
    wioe5_status_t st = check_can_start(dev);
    if (st != WIOE5_OK) {
        return st;
    }
    if ((fport == 0u) || (fport > 223u) || (len > WIOE5_MAX_PAYLOAD) ||
        ((data == NULL) && (len > 0u))) {
        return WIOE5_ERR_PARAM;
    }
    if (!dev->setup_done || (dev->state == ST_UNINIT)) {
        return WIOE5_ERR_STATE;
    }
    if (len > 0u) {
        memcpy(dev->payload, data, len);
    }
    dev->payload_len       = len;
    dev->payload_port      = fport;
    dev->payload_confirmed = confirmed;
    return begin(dev, WIOE5_OP_SEND);
}

wioe5_status_t wioe5_start_sleep(wioe5_t *dev)
{
    wioe5_status_t st = check_can_start(dev);
    if (st != WIOE5_OK) {
        return st;
    }
    if (!dev->setup_done || (dev->state != ST_IDLE)) {
        return WIOE5_ERR_STATE;
    }
    return begin(dev, WIOE5_OP_SLEEP);
}

wioe5_status_t wioe5_start_wake(wioe5_t *dev)
{
    wioe5_status_t st = check_can_start(dev);
    if (st != WIOE5_OK) {
        return st;
    }
    if (dev->state != ST_SLEEPING) {
        return WIOE5_ERR_STATE;
    }
    return begin(dev, WIOE5_OP_WAKE);
}

wioe5_status_t wioe5_start_reset(wioe5_t *dev)
{
    wioe5_status_t st = check_can_start(dev);
    if (st != WIOE5_OK) {
        return st;
    }
    return begin(dev, WIOE5_OP_RESET);
}

bool wioe5_is_busy(const wioe5_t *dev)
{
    return (dev != NULL) && ((wioe5_op_t)dev->op != WIOE5_OP_NONE);
}

bool wioe5_can_stop(const wioe5_t *dev)
{
    if ((dev == NULL) || wioe5_is_busy(dev) || !wioe5_rb_is_empty(&dev->rb)) {
        return false;
    }
    return (dev->state == ST_UNINIT) || (dev->state == ST_SLEEPING);
}

wioe5_status_t wioe5_last_result(const wioe5_t *dev)
{
    return (dev != NULL) ? dev->result : WIOE5_ERR_PARAM;
}

const wioe5_info_t *wioe5_info(const wioe5_t *dev)
{
    return (dev != NULL) ? &dev->info : NULL;
}

wioe5_status_t wioe5_wait(wioe5_t *dev, void (*idle_hook)(void))
{
    if (dev == NULL) {
        return WIOE5_ERR_PARAM;
    }
    while (wioe5_is_busy(dev)) {
        wioe5_process(dev);
        if (wioe5_is_busy(dev) && (idle_hook != NULL)) {
            idle_hook();
        }
    }
    return dev->result;
}
