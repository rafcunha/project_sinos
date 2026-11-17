/* STUB: interface only. Implementation pending (development phase). */
#include "wioe5_parse.h"

bool wioe5_parse_resp(char *line, wioe5_resp_t *out)
{
    (void)line; (void)out;
    return false;
}

bool wioe5_resp_name_is(const wioe5_resp_t *r, const char *name)
{
    (void)r; (void)name;
    return false;
}

bool wioe5_parse_ver(const char *body, char *out, size_t cap)
{
    (void)body; (void)out; (void)cap;
    return false;
}

bool wioe5_parse_id(const char *body, const char *field, uint8_t *out, size_t n)
{
    (void)body; (void)field; (void)out; (void)n;
    return false;
}

bool wioe5_parse_lw_len(const char *body, uint16_t *len)
{
    (void)body; (void)len;
    return false;
}

bool wioe5_parse_uint_body(const char *body, uint32_t *value)
{
    (void)body; (void)value;
    return false;
}

bool wioe5_parse_event(const char *body, wioe5_ev_t *ev)
{
    (void)body;
    if (ev != 0) {
        ev->kind = WIOE5_EV_UNKNOWN;
    }
    return false;
}

bool wioe5_parse_centi(const char *s, size_t len, int16_t *out)
{
    (void)s; (void)len; (void)out;
    return false;
}
