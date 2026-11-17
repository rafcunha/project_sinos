#include "test_common.h"

#include <string.h>

cb_capture_t g_cb;
rig_t       *g_rig;

void cb_reset(void)
{
    memset(&g_cb, 0, sizeof(g_cb));
}

int cb_event_count(wioe5_event_t evt)
{
    int n = 0;
    for (int i = 0; i < g_cb.n_events && i < CAP_MAX_EVENTS; i++) {
        if (g_cb.events[i] == evt) {
            n++;
        }
    }
    return n;
}

static void cap_op_done(void *user, wioe5_op_t op, wioe5_status_t status)
{
    (void)user;
    g_cb.op_done_count++;
    g_cb.last_op     = op;
    g_cb.last_status = status;
    g_cb.op_done_at  = (g_rig != NULL) ? g_rig->fp.now : 0u;
    g_cb.op_done_seq = ++g_cb.seq;
}

static void cap_downlink(void *user, const wioe5_downlink_t *dl)
{
    (void)user;
    g_cb.dl_count++;
    g_cb.dl_seq  = ++g_cb.seq;
    g_cb.dl_port = dl->port;
    g_cb.dl_len  = dl->len <= sizeof(g_cb.dl_data) ? dl->len : sizeof(g_cb.dl_data);
    if (dl->data != NULL) {
        memcpy(g_cb.dl_data, dl->data, g_cb.dl_len);
    }
    g_cb.dl_rssi   = dl->rssi_dbm;
    g_cb.dl_snr    = dl->snr_cdb;
    g_cb.dl_window = dl->rx_window;
}

static void cap_event(void *user, wioe5_event_t evt)
{
    (void)user;
    if (g_cb.n_events < CAP_MAX_EVENTS) {
        g_cb.events[g_cb.n_events] = evt;
    }
    g_cb.n_events++;
    ++g_cb.seq;
}

void rig_prepare(rig_t *r, bool reset_pin)
{
    memset(r, 0, sizeof(*r));
    g_rig = r;
    cb_reset();
    fake_port_init(&r->fp, reset_pin);
    wioe5_config_default(&r->cfg);
    r->cfg.on_op_done  = cap_op_done;
    r->cfg.on_downlink = cap_downlink;
    r->cfg.on_event    = cap_event;
    r->cfg.user        = r;
}

wioe5_status_t rig_init(rig_t *r, bool reset_pin)
{
    rig_prepare(r, reset_pin);
    wioe5_port_t   port = fake_port_iface(&r->fp);
    wioe5_status_t st   = wioe5_init(&r->dev, &port, &r->cfg);
    sim_init(&r->sim, &r->fp, &r->dev);
    return st;
}

void rig_new_script(rig_t *r)
{
    sim_init(&r->sim, &r->fp, &r->dev);
}

void rig_run(rig_t *r, const sim_step_t *steps, size_t n, uint32_t max_ms)
{
    sim_add(&r->sim, steps, n);
    (void)sim_run_until_idle(&r->sim, max_ms);
}

bool rig_setup(rig_t *r)
{
    rig_new_script(r);
    if (wioe5_start_setup(&r->dev) != WIOE5_OK) {
        return false;
    }
    rig_run(r, SCRIPT_SETUP, SCRIPT_SETUP_N, 60000u);
    return sim_done(&r->sim) && wioe5_last_result(&r->dev) == WIOE5_OK;
}

bool rig_setup_join(rig_t *r)
{
    if (!rig_setup(r)) {
        return false;
    }
    rig_new_script(r);
    if (wioe5_start_join(&r->dev, false) != WIOE5_OK) {
        return false;
    }
    rig_run(r, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N, 60000u);
    return sim_done(&r->sim) && wioe5_last_result(&r->dev) == WIOE5_OK;
}

bool rig_setup_join_sleep(rig_t *r)
{
    if (!rig_setup_join(r)) {
        return false;
    }
    rig_new_script(r);
    if (wioe5_start_sleep(&r->dev) != WIOE5_OK) {
        return false;
    }
    rig_run(r, SCRIPT_SLEEP_OK, SCRIPT_SLEEP_OK_N, 60000u);
    bool ok = sim_done(&r->sim) && wioe5_last_result(&r->dev) == WIOE5_OK;
    cb_reset();
    return ok;
}

uint32_t tx_ms(size_t len)
{
    return (uint32_t)((len * 11u + 9u) / 10u);
}

/* ---- Canned scripts ------------------------------------------------------ */

const sim_step_t SCRIPT_SETUP[] = {
    {"AT\r\n", {{5, "+AT: OK", false}}},
    {"AT+LOG=QUIET\r\n", {{5, "+LOG: QUIET", false}}},
    {"AT+VER\r\n", {{5, "+VER: 4.0.11", false}}},
    {"AT+ID=DevEui\r\n", {{5, "+ID: DevEui, " DEVEUI_TEXT, false}}},
    {"AT+MODE=LWOTAA\r\n", {{20, "+MODE: LWOTAA", false}}},
    {"AT+DR=AU915\r\n", {{20, "+DR: AU915", false}}},
    {"AT+CH=NUM,8-15,65\r\n", {{20, "+CH: NUM, 8-15, 65", false}}},
    {"AT+ADR=ON\r\n", {{20, "+ADR: ON", false}}},
    {"AT+PORT=1\r\n", {{20, "+PORT: 1", false}}},
    {"AT+RETRY=3\r\n", {{20, "+RETRY: 3", false}}},
    {"AT+REPT=1\r\n", {{20, "+REPT: 1", false}}},
};
const size_t SCRIPT_SETUP_N = SIM_COUNT(SCRIPT_SETUP);

const sim_step_t SCRIPT_JOIN_OK[] = {
    {"AT+JOIN\r\n",
     {{10, "+JOIN: Start", false},
      {10, "+JOIN: NORMAL", false},
      {6000, "+JOIN: NetID 000013 DevAddr 26:0B:12:34", false},
      {5, "+JOIN: Done", false}}},
};
const size_t SCRIPT_JOIN_OK_N = SIM_COUNT(SCRIPT_JOIN_OK);

const sim_step_t SCRIPT_SLEEP_OK[] = {
    {"AT+LOWPOWER\r\n", {{5, "+LOWPOWER: SLEEP", false}}},
};
const size_t SCRIPT_SLEEP_OK_N = SIM_COUNT(SCRIPT_SLEEP_OK);

const sim_step_t SCRIPT_WAKE_OK[] = {
    {"\xFF", {{2, "\x55", true}, {15, "+LOWPOWER: WAKEUP", false}}},
    {"AT\r\n", {{5, "+AT: OK", false}}},
};
const size_t SCRIPT_WAKE_OK_N = SIM_COUNT(SCRIPT_WAKE_OK);
