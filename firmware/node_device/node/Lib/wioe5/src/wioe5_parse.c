/**
 * @file wioe5_parse.c
 * @brief Pure response parsers (driver spec sections 9.2 and 9.3).
 *
 * All matching is case-insensitive. Parsers work on a small cursor over a
 * NUL-terminated body and never write outside the provided outputs.
 */
#include "wioe5_parse.h"

#include <string.h>

/* ---- Character helpers ---------------------------------------------------- */

static char to_upper(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - ('a' - 'A')) : c;
}

static bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

static bool is_alnum(char c)
{
    return is_digit(c) || ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
}

static bool is_space(char c)
{
    return (c == ' ') || (c == '\t');
}

static int hex_val(char c)
{
    if (is_digit(c)) {
        return c - '0';
    }
    c = to_upper(c);
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    return -1;
}

static bool str_ieq(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        if (to_upper(*a) != to_upper(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0') && (*b == '\0');
}

/* ---- Cursor --------------------------------------------------------------- */

typedef struct {
    const char *p;
    const char *end;
} cur_t;

static cur_t cur_of(const char *s)
{
    cur_t c;
    c.p   = s;
    c.end = s + strlen(s);
    return c;
}

static bool c_eof(const cur_t *c)
{
    return c->p >= c->end;
}

static void c_skip_sp(cur_t *c)
{
    while (!c_eof(c) && is_space(*c->p)) {
        c->p++;
    }
}

/** Case-insensitive literal. Advances only on success. */
static bool c_lit(cur_t *c, const char *lit)
{
    const char *p = c->p;
    while (*lit != '\0') {
        if ((p >= c->end) || (to_upper(*p) != to_upper(*lit))) {
            return false;
        }
        p++;
        lit++;
    }
    c->p = p;
    return true;
}

static bool c_char(cur_t *c, char ch)
{
    if (!c_eof(c) && (*c->p == ch)) {
        c->p++;
        return true;
    }
    return false;
}

static bool c_uint(cur_t *c, uint32_t *v)
{
    uint32_t acc = 0u;
    bool     any = false;
    while (!c_eof(c) && is_digit(*c->p)) {
        uint32_t d = (uint32_t)(*c->p - '0');
        if (acc > (UINT32_MAX - d) / 10u) {
            return false;
        }
        acc = acc * 10u + d;
        any = true;
        c->p++;
    }
    if (any) {
        *v = acc;
    }
    return any;
}

static bool c_int16(cur_t *c, int16_t *v)
{
    bool     neg = c_char(c, '-');
    uint32_t u   = 0u;
    if (!c_uint(c, &u)) {
        return false;
    }
    if (neg) {
        if (u > 32768u) {
            return false;
        }
        *v = (int16_t)(-(int32_t)u);
    } else {
        if (u > 32767u) {
            return false;
        }
        *v = (int16_t)u;
    }
    return true;
}

static bool c_hexbyte(cur_t *c, uint8_t *b)
{
    if ((c->end - c->p) < 2) {
        return false;
    }
    int hi = hex_val(c->p[0]);
    int lo = hex_val(c->p[1]);
    if ((hi < 0) || (lo < 0)) {
        return false;
    }
    *b = (uint8_t)((hi << 4) | lo);
    c->p += 2;
    return true;
}

/** "XX:XX:...:XX" with exactly n bytes; spaces around ':' tolerated. */
static bool c_hex_bytes_colon(cur_t *c, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (i > 0u) {
            c_skip_sp(c);
            if (!c_char(c, ':')) {
                return false;
            }
            c_skip_sp(c);
        }
        if (!c_hexbyte(c, &out[i])) {
            return false;
        }
    }
    return true;
}

static bool c_end(cur_t *c)
{
    c_skip_sp(c);
    return c_eof(c);
}

/** Exact keyword match of the whole body (surrounding spaces allowed). */
static bool body_is(const char *body, const char *kw)
{
    cur_t c = cur_of(body);
    c_skip_sp(&c);
    return c_lit(&c, kw) && c_end(&c);
}

/* ---- Generic line --------------------------------------------------------- */

bool wioe5_parse_resp(char *line, wioe5_resp_t *out)
{
    if ((line == NULL) || (out == NULL) || (line[0] != '+')) {
        return false;
    }
    size_t i = 1u;
    while (is_alnum(line[i])) {
        i++;
    }
    size_t name_len = i - 1u;
    if ((name_len == 0u) || (name_len > WIOE5_NAME_MAX) || (line[i] != ':')) {
        return false;
    }
    memcpy(out->name, &line[1], name_len);
    out->name[name_len] = '\0';

    char *body = &line[i + 1u];
    while (is_space(*body)) {
        body++;
    }
    char *end = body + strlen(body);
    while ((end > body) && is_space(end[-1])) {
        end--;
    }
    *end = '\0';

    out->body       = body;
    out->is_error   = false;
    out->error_code = 0;

    cur_t   c    = cur_of(body);
    int16_t code = 0;
    if (c_lit(&c, "ERROR(") && c_int16(&c, &code) && c_char(&c, ')') && c_eof(&c)) {
        out->is_error   = true;
        out->error_code = code;
    }
    return true;
}

bool wioe5_resp_name_is(const wioe5_resp_t *r, const char *name)
{
    return (r != NULL) && (name != NULL) && str_ieq(r->name, name);
}

/* ---- Simple bodies -------------------------------------------------------- */

bool wioe5_parse_ver(const char *body, char *out, size_t cap)
{
    if ((body == NULL) || (out == NULL)) {
        return false;
    }
    size_t n = strlen(body);
    if ((n == 0u) || (n >= cap)) {
        return false;
    }
    memcpy(out, body, n + 1u);
    return true;
}

bool wioe5_parse_id(const char *body, const char *field, uint8_t *out, size_t n)
{
    uint8_t tmp[16];
    if ((body == NULL) || (field == NULL) || (out == NULL) || (n == 0u) || (n > sizeof(tmp))) {
        return false;
    }
    cur_t c = cur_of(body);
    c_skip_sp(&c);
    if (!c_lit(&c, field)) {
        return false;
    }
    c_skip_sp(&c);
    if (!c_char(&c, ',')) {
        return false;
    }
    c_skip_sp(&c);
    if (!c_hex_bytes_colon(&c, tmp, n) || !c_end(&c)) {
        return false;
    }
    memcpy(out, tmp, n);
    return true;
}

bool wioe5_parse_lw_len(const char *body, uint16_t *len)
{
    uint32_t v = 0u;
    if ((body == NULL) || (len == NULL)) {
        return false;
    }
    cur_t c = cur_of(body);
    c_skip_sp(&c);
    if (!c_lit(&c, "LEN")) {
        return false;
    }
    c_skip_sp(&c);
    if (!c_char(&c, ',')) {
        return false;
    }
    c_skip_sp(&c);
    if (!c_uint(&c, &v) || (v > UINT16_MAX) || !c_end(&c)) {
        return false;
    }
    *len = (uint16_t)v;
    return true;
}

bool wioe5_parse_uint_body(const char *body, uint32_t *value)
{
    uint32_t v = 0u;
    if ((body == NULL) || (value == NULL)) {
        return false;
    }
    cur_t c = cur_of(body);
    c_skip_sp(&c);
    if (!c_uint(&c, &v) || !c_end(&c)) {
        return false;
    }
    *value = v;
    return true;
}

bool wioe5_parse_centi(const char *s, size_t len, int16_t *out)
{
    if ((s == NULL) || (out == NULL) || (len == 0u)) {
        return false;
    }
    size_t i   = 0u;
    bool   neg = false;
    if (s[0] == '-') {
        neg = true;
        i++;
    }
    int32_t ip  = 0;
    bool    any = false;
    while ((i < len) && is_digit(s[i])) {
        ip  = ip * 10 + (s[i] - '0');
        any = true;
        if (ip > 400) {
            return false; /* far outside any RSSI/SNR value */
        }
        i++;
    }
    if (!any) {
        return false;
    }
    int32_t frac = 0;
    if ((i < len) && (s[i] == '.')) {
        i++;
        int digits = 0;
        while ((i < len) && is_digit(s[i])) {
            if (digits < 2) {
                frac = frac * 10 + (s[i] - '0');
            }
            digits++;
            i++;
        }
        if (digits == 0) {
            return false;
        }
        if (digits == 1) {
            frac *= 10;
        }
    }
    if (i != len) {
        return false;
    }
    int32_t v = ip * 100 + frac;
    *out      = (int16_t)(neg ? -v : v);
    return true;
}

/* ---- JOIN / MSG family ---------------------------------------------------- */

static bool parse_netid(cur_t *c, wioe5_ev_t *ev)
{
    uint32_t id     = 0u;
    int      digits = 0;
    c_skip_sp(c);
    while (!c_eof(c) && (hex_val(*c->p) >= 0)) {
        if (digits >= 8) {
            return false;
        }
        id = (id << 4) | (uint32_t)hex_val(*c->p);
        digits++;
        c->p++;
    }
    if (digits == 0) {
        return false;
    }
    c_skip_sp(c);
    if (!c_lit(c, "DevAddr")) {
        return false;
    }
    c_skip_sp(c);
    if (!c_hex_bytes_colon(c, ev->dev_addr, 4u) || !c_end(c)) {
        return false;
    }
    ev->net_id = id;
    return true;
}

static bool parse_link(cur_t *c, wioe5_ev_t *ev)
{
    uint32_t m = 0u;
    uint32_t g = 0u;
    c_skip_sp(c);
    if (!c_uint(c, &m) || (m > 255u)) {
        return false;
    }
    c_skip_sp(c);
    if (!c_char(c, ',')) {
        return false;
    }
    c_skip_sp(c);
    if (!c_uint(c, &g) || (g > 255u) || !c_end(c)) {
        return false;
    }
    ev->link_margin = (uint8_t)m;
    ev->link_gw     = (uint8_t)g;
    return true;
}

static bool parse_downlink(cur_t *c, wioe5_ev_t *ev)
{
    uint32_t port = 0u;
    c_skip_sp(c);
    if (!c_uint(c, &port) || (port > 255u)) {
        return false;
    }
    c_skip_sp(c);
    if (!c_char(c, ';')) {
        return false;
    }
    c_skip_sp(c);
    if (!c_lit(c, "RX:")) {
        return false;
    }
    c_skip_sp(c);
    if (!c_char(c, '"')) {
        return false;
    }
    const char *start = c->p;
    while (!c_eof(c) && (*c->p != '"')) {
        c->p++;
    }
    if (c_eof(c)) {
        return false; /* no closing quote */
    }
    ev->hex     = start;
    ev->hex_len = (size_t)(c->p - start);
    c->p++;
    if (!c_end(c)) {
        return false;
    }
    ev->port = (uint8_t)port;
    return true;
}

static bool parse_rxinfo(cur_t *c, wioe5_ev_t *ev)
{
    if (c_eof(c) || !is_digit(*c->p)) {
        return false;
    }
    ev->rx_window = (uint8_t)(*c->p - '0');
    c->p++;
    c_skip_sp(c);
    if (!c_char(c, ',')) {
        return false;
    }
    c_skip_sp(c);
    if (!c_lit(c, "RSSI")) {
        return false;
    }
    c_skip_sp(c);
    if (!c_int16(c, &ev->rssi_dbm)) {
        return false;
    }
    c_skip_sp(c);
    if (!c_char(c, ',')) {
        return false;
    }
    c_skip_sp(c);
    if (!c_lit(c, "SNR")) {
        return false;
    }
    c_skip_sp(c);
    const char *s = c->p;
    const char *e = c->end;
    while ((e > s) && is_space(e[-1])) {
        e--;
    }
    return wioe5_parse_centi(s, (size_t)(e - s), &ev->snr_cdb);
}

typedef struct {
    const char     *kw;
    wioe5_ev_kind_t kind;
} kw_t;

static const kw_t EXACT_KW[] = {
    {"Start", WIOE5_EV_START},
    {"Starting", WIOE5_EV_START},
    {"Done", WIOE5_EV_DONE},
    {"NORMAL", WIOE5_EV_NORMAL},
    {"Join failed", WIOE5_EV_JOIN_FAILED},
    {"Joined already", WIOE5_EV_JOINED_ALREADY},
    {"Wait ACK", WIOE5_EV_WAIT_ACK},
    {"FPENDING", WIOE5_EV_FPENDING},
    {"ACK Received", WIOE5_EV_ACK},
    {"MULTICAST", WIOE5_EV_MULTICAST},
    {"LoRaWAN modem is busy", WIOE5_EV_ERR_BUSY},
    {"Please join network first", WIOE5_EV_ERR_NOT_JOINED},
    {"DR error", WIOE5_EV_ERR_DR},
};

bool wioe5_parse_event(const char *body, wioe5_ev_t *ev)
{
    if (ev == NULL) {
        return false;
    }
    memset(ev, 0, sizeof(*ev));
    ev->kind = WIOE5_EV_UNKNOWN;
    if (body == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(EXACT_KW) / sizeof(EXACT_KW[0]); i++) {
        if (body_is(body, EXACT_KW[i].kw)) {
            ev->kind = EXACT_KW[i].kind;
            return true;
        }
    }

    cur_t c = cur_of(body);
    c_skip_sp(&c);
    cur_t    save = c;
    uint32_t v    = 0u;

    if (c_lit(&c, "No free channel")) {
        ev->kind = WIOE5_EV_ERR_NO_FREE_CH;
        return true;
    }
    c = save;
    if (c_lit(&c, "No band in")) {
        c_skip_sp(&c);
        if (c_uint(&c, &v)) {
            c_skip_sp(&c);
            if (c_lit(&c, "ms") && c_end(&c)) {
                ev->kind  = WIOE5_EV_ERR_NO_BAND;
                ev->value = v;
                return true;
            }
        }
        return false;
    }
    c = save;
    if (c_lit(&c, "Length error")) {
        c_skip_sp(&c);
        if (c_uint(&c, &v) && c_end(&c)) {
            ev->kind  = WIOE5_EV_ERR_LENGTH;
            ev->value = v;
            return true;
        }
        return false;
    }
    c = save;
    if (c_lit(&c, "NetID")) {
        if (parse_netid(&c, ev)) {
            ev->kind = WIOE5_EV_NETID;
            return true;
        }
        return false;
    }
    c = save;
    if (c_lit(&c, "Link")) {
        if (parse_link(&c, ev)) {
            ev->kind = WIOE5_EV_LINK;
            return true;
        }
        return false;
    }
    c = save;
    if (c_lit(&c, "PORT:")) {
        if (parse_downlink(&c, ev)) {
            ev->kind = WIOE5_EV_DOWNLINK;
            return true;
        }
        memset(ev, 0, sizeof(*ev));
        ev->kind = WIOE5_EV_UNKNOWN;
        return false;
    }
    c = save;
    if (c_lit(&c, "RXWIN")) {
        if (parse_rxinfo(&c, ev)) {
            ev->kind = WIOE5_EV_RXINFO;
            return true;
        }
        memset(ev, 0, sizeof(*ev));
        ev->kind = WIOE5_EV_UNKNOWN;
        return false;
    }
    c = save;
    if (c_lit(&c, "TX") && (c_eof(&c) || is_space(*c.p))) {
        ev->kind = WIOE5_EV_TX;
        return true;
    }
    return false;
}
