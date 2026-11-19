/**
 * @file node_payload.c
 * @brief Uplink payload codec (docs/payload/payload_spec.md).
 */
#include "node_payload.h"

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

size_t node_payload_encode_heartbeat(const node_payload_heartbeat_t *m, uint8_t *buf, size_t cap)
{
    if ((m == NULL) || (buf == NULL) || (cap < NODE_PAYLOAD_HEARTBEAT_LEN)) {
        return 0u;
    }
    buf[0] = NODE_PAYLOAD_HEADER(NODE_PAYLOAD_VERSION, NODE_PAYLOAD_HEARTBEAT);
    put_u16(&buf[1], m->counter);
    return NODE_PAYLOAD_HEARTBEAT_LEN;
}

size_t node_payload_encode_measurement(const node_payload_measurement_t *m, uint8_t *buf,
                                       size_t cap)
{
    if ((m == NULL) || (buf == NULL) || (cap < NODE_PAYLOAD_MEASUREMENT_LEN)) {
        return 0u;
    }
    buf[0] = NODE_PAYLOAD_HEADER(NODE_PAYLOAD_VERSION, NODE_PAYLOAD_MEASUREMENT);
    put_u16(&buf[1], m->distance_mm);
    put_u16(&buf[3], m->battery_mv);
    put_u16(&buf[5], (uint16_t)m->temperature_dc); /* two's complement on the wire */
    buf[7] = (uint8_t)(m->flags & NODE_PAYLOAD_FLAGS_MASK);
    buf[8] = m->samples;
    return NODE_PAYLOAD_MEASUREMENT_LEN;
}

size_t node_payload_encode_status(const node_payload_status_t *m, uint8_t *buf, size_t cap)
{
    if ((m == NULL) || (buf == NULL) || (cap < NODE_PAYLOAD_STATUS_LEN)) {
        return 0u;
    }
    buf[0] = NODE_PAYLOAD_HEADER(NODE_PAYLOAD_VERSION, NODE_PAYLOAD_STATUS);
    buf[1] = m->fw_major;
    buf[2] = m->fw_minor;
    buf[3] = m->fw_patch;
    buf[4] = m->reset_cause;
    put_u16(&buf[5], m->uptime_h);
    buf[7] = m->join_failures;
    buf[8] = m->uplink_failures;
    buf[9] = m->module_resets;
    return NODE_PAYLOAD_STATUS_LEN;
}

size_t node_payload_encode(const node_payload_msg_t *msg, uint8_t *buf, size_t cap)
{
    if (msg == NULL) {
        return 0u;
    }
    switch (msg->type) {
    case NODE_PAYLOAD_HEARTBEAT:
        return node_payload_encode_heartbeat(&msg->u.heartbeat, buf, cap);
    case NODE_PAYLOAD_MEASUREMENT:
        return node_payload_encode_measurement(&msg->u.measurement, buf, cap);
    case NODE_PAYLOAD_STATUS:
        return node_payload_encode_status(&msg->u.status, buf, cap);
    default:
        return 0u;
    }
}

node_payload_result_t node_payload_decode(const uint8_t *buf, size_t len, node_payload_msg_t *out)
{
    if ((out == NULL) || ((buf == NULL) && (len > 0u))) {
        return NODE_PAYLOAD_ERR_PARAM;
    }
    if (len == 0u) {
        return NODE_PAYLOAD_ERR_EMPTY;
    }

    uint8_t type    = (uint8_t)(buf[0] & 0x1Fu);
    uint8_t version = (uint8_t)(buf[0] >> 5);
    size_t  expected;

    switch (type) {
    case NODE_PAYLOAD_HEARTBEAT:
        expected = NODE_PAYLOAD_HEARTBEAT_LEN;
        break;
    case NODE_PAYLOAD_MEASUREMENT:
        expected = NODE_PAYLOAD_MEASUREMENT_LEN;
        break;
    case NODE_PAYLOAD_STATUS:
        expected = NODE_PAYLOAD_STATUS_LEN;
        break;
    default:
        return NODE_PAYLOAD_ERR_UNKNOWN_TYPE;
    }
    if (version != NODE_PAYLOAD_VERSION) {
        return NODE_PAYLOAD_ERR_UNKNOWN_VERSION;
    }
    if (len != expected) {
        return NODE_PAYLOAD_ERR_BAD_LENGTH;
    }

    out->type    = (node_payload_type_t)type;
    out->version = version;
    switch (out->type) {
    case NODE_PAYLOAD_HEARTBEAT:
        out->u.heartbeat.counter = get_u16(&buf[1]);
        break;
    case NODE_PAYLOAD_MEASUREMENT:
        out->u.measurement.distance_mm    = get_u16(&buf[1]);
        out->u.measurement.battery_mv     = get_u16(&buf[3]);
        out->u.measurement.temperature_dc = (int16_t)get_u16(&buf[5]);
        out->u.measurement.flags          = buf[7]; /* reserved bits kept raw */
        out->u.measurement.samples        = buf[8];
        break;
    case NODE_PAYLOAD_STATUS:
    default:
        out->u.status.fw_major        = buf[1];
        out->u.status.fw_minor        = buf[2];
        out->u.status.fw_patch        = buf[3];
        out->u.status.reset_cause     = buf[4];
        out->u.status.uptime_h        = get_u16(&buf[5]);
        out->u.status.join_failures   = buf[7];
        out->u.status.uplink_failures = buf[8];
        out->u.status.module_resets   = buf[9];
        break;
    }
    return NODE_PAYLOAD_OK;
}

uint8_t node_payload_sat_u8(uint32_t v)
{
    return (v > 0xFFu) ? 0xFFu : (uint8_t)v;
}

uint16_t node_payload_uptime_h(uint64_t uptime_ms)
{
    uint64_t h = uptime_ms / 3600000u;
    return (h >= NODE_PAYLOAD_UPTIME_UNKNOWN) ? (uint16_t)(NODE_PAYLOAD_UPTIME_UNKNOWN - 1u)
                                              : (uint16_t)h;
}

/** Parses 0..255 decimal digits at *p; advances *p. */
static bool parse_part(const char **p, uint8_t *out)
{
    const char *s = *p;
    uint32_t    v = 0u;
    if ((*s < '0') || (*s > '9')) {
        return false;
    }
    while ((*s >= '0') && (*s <= '9')) {
        v = (v * 10u) + (uint32_t)(*s - '0');
        if (v > 255u) {
            return false;
        }
        s++;
    }
    *out = (uint8_t)v;
    *p   = s;
    return true;
}

bool node_payload_parse_version(const char *s, uint8_t *major, uint8_t *minor, uint8_t *patch)
{
    uint8_t ma = 0u;
    uint8_t mi = 0u;
    uint8_t pa = 0u;
    bool    ok = false;

    if ((s != NULL) && (major != NULL) && (minor != NULL) && (patch != NULL)) {
        if ((*s == 'v') || (*s == 'V')) {
            s++;
        }
        ok = parse_part(&s, &ma) && (*s++ == '.') && parse_part(&s, &mi) && (*s++ == '.') &&
             parse_part(&s, &pa) && ((*s == '\0') || (*s == '-') || (*s == '+'));
        if (!ok) {
            ma = mi = pa = 0u;
        }
        *major = ma;
        *minor = mi;
        *patch = pa;
    }
    return ok;
}
