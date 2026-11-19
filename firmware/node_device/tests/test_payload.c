/* node_payload codec (docs/payload/payload_spec.md, test plan section 2.11).
 * The vectors come from fixtures/payload_vectors.json, shared with the TTN
 * formatter tests (ttn/uplink_formatter.test.js). */
#include <stdio.h>
#include <string.h>

#include "node_payload.h"
#include "payload_vectors.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define N_ITEMS(a) (sizeof(a) / sizeof((a)[0]))

static int hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) return c - '0';
    if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
    if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
    return -1;
}

static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = strlen(hex);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, n % 2u, hex);
    TEST_ASSERT_TRUE_MESSAGE(n / 2u <= cap, hex);
    for (size_t i = 0; i < n / 2u; i++) {
        int hi = hex_nibble(hex[2u * i]);
        int lo = hex_nibble(hex[2u * i + 1u]);
        TEST_ASSERT_TRUE_MESSAGE((hi >= 0) && (lo >= 0), hex);
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return n / 2u;
}

/** Raw value of a decoded field by its JSON name. */
static long field(const node_payload_msg_t *m, const char *k, const char *vec)
{
    const node_payload_measurement_t *me = &m->u.measurement;
    const node_payload_status_t      *st = &m->u.status;
    if (strcmp(k, "type") == 0) return (long)m->type;
    if (strcmp(k, "version") == 0) return (long)m->version;
    switch (m->type) {
    case NODE_PAYLOAD_HEARTBEAT:
        if (strcmp(k, "counter") == 0) return m->u.heartbeat.counter;
        break;
    case NODE_PAYLOAD_MEASUREMENT:
        if (strcmp(k, "distance_mm") == 0) return me->distance_mm;
        if (strcmp(k, "battery_mv") == 0) return me->battery_mv;
        if (strcmp(k, "temperature_dc") == 0) return me->temperature_dc;
        if (strcmp(k, "flags") == 0) return me->flags;
        if (strcmp(k, "samples") == 0) return me->samples;
        break;
    case NODE_PAYLOAD_STATUS:
        if (strcmp(k, "fw_major") == 0) return st->fw_major;
        if (strcmp(k, "fw_minor") == 0) return st->fw_minor;
        if (strcmp(k, "fw_patch") == 0) return st->fw_patch;
        if (strcmp(k, "reset_cause") == 0) return st->reset_cause;
        if (strcmp(k, "uptime_h") == 0) return st->uptime_h;
        if (strcmp(k, "join_failures") == 0) return st->join_failures;
        if (strcmp(k, "uplink_failures") == 0) return st->uplink_failures;
        if (strcmp(k, "module_resets") == 0) return st->module_resets;
        break;
    default:
        break;
    }
    char msg[96];
    (void)snprintf(msg, sizeof(msg), "%s: unknown field '%s'", vec, k);
    TEST_FAIL_MESSAGE(msg);
    return 0;
}

static node_payload_result_t error_code(const char *name)
{
    if (strcmp(name, "empty") == 0) return NODE_PAYLOAD_ERR_EMPTY;
    if (strcmp(name, "unknown_type") == 0) return NODE_PAYLOAD_ERR_UNKNOWN_TYPE;
    if (strcmp(name, "unknown_version") == 0) return NODE_PAYLOAD_ERR_UNKNOWN_VERSION;
    if (strcmp(name, "bad_length") == 0) return NODE_PAYLOAD_ERR_BAD_LENGTH;
    TEST_FAIL_MESSAGE(name);
    return NODE_PAYLOAD_OK;
}

/* TC-PL-01: every valid vector decodes to the expected raw fields. */
static void test_vectors_decode(void)
{
    TEST_ASSERT_TRUE(N_ITEMS(PV_VALID) >= 10u);
    for (size_t i = 0; i < N_ITEMS(PV_VALID); i++) {
        const pv_valid_t  *v = &PV_VALID[i];
        uint8_t            bytes[NODE_PAYLOAD_MAX_LEN + 4u];
        size_t             len = hex_to_bytes(v->hex, bytes, sizeof(bytes));
        node_payload_msg_t m;
        TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_PAYLOAD_OK, node_payload_decode(bytes, len, &m), v->name);
        for (int f = 0; f < v->nfields; f++) {
            TEST_ASSERT_EQUAL_INT32_MESSAGE(v->fields[f].value, field(&m, v->fields[f].key, v->name),
                                            v->name);
        }
    }
}

/* TC-PL-02: encoding the decoded message gives back the same bytes. */
static void test_vectors_roundtrip(void)
{
    for (size_t i = 0; i < N_ITEMS(PV_VALID); i++) {
        const pv_valid_t *v = &PV_VALID[i];
        if (!v->roundtrip) {
            continue;
        }
        uint8_t            bytes[NODE_PAYLOAD_MAX_LEN + 4u];
        uint8_t            enc[NODE_PAYLOAD_MAX_LEN];
        size_t             len = hex_to_bytes(v->hex, bytes, sizeof(bytes));
        node_payload_msg_t m;
        TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_OK, node_payload_decode(bytes, len, &m));
        TEST_ASSERT_EQUAL_size_t_MESSAGE(len, node_payload_encode(&m, enc, sizeof(enc)), v->name);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(bytes, enc, len, v->name);
    }
}

/* TC-PL-03: every invalid vector is rejected with the expected error. */
static void test_vectors_invalid(void)
{
    TEST_ASSERT_TRUE(N_ITEMS(PV_INVALID) >= 5u);
    for (size_t i = 0; i < N_ITEMS(PV_INVALID); i++) {
        const pv_invalid_t *v = &PV_INVALID[i];
        uint8_t             bytes[NODE_PAYLOAD_MAX_LEN + 4u];
        size_t              len = hex_to_bytes(v->hex, bytes, sizeof(bytes));
        node_payload_msg_t  m;
        TEST_ASSERT_EQUAL_INT_MESSAGE(error_code(v->error), node_payload_decode(bytes, len, &m),
                                      v->name);
    }
}

/* TC-PL-04: every message fits the 11-byte limit; header bytes are as specified. */
static void test_sizes_and_headers(void)
{
    TEST_ASSERT_TRUE(NODE_PAYLOAD_HEARTBEAT_LEN <= NODE_PAYLOAD_MAX_LEN);
    TEST_ASSERT_TRUE(NODE_PAYLOAD_MEASUREMENT_LEN <= NODE_PAYLOAD_MAX_LEN);
    TEST_ASSERT_TRUE(NODE_PAYLOAD_STATUS_LEN <= NODE_PAYLOAD_MAX_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x00, NODE_PAYLOAD_HEADER(0, NODE_PAYLOAD_HEARTBEAT));
    TEST_ASSERT_EQUAL_HEX8(0x01, NODE_PAYLOAD_HEADER(0, NODE_PAYLOAD_MEASUREMENT));
    TEST_ASSERT_EQUAL_HEX8(0x02, NODE_PAYLOAD_HEADER(0, NODE_PAYLOAD_STATUS));
    TEST_ASSERT_EQUAL_HEX8(0xE1, NODE_PAYLOAD_HEADER(7, 1));
    TEST_ASSERT_EQUAL_HEX8(0x3F, NODE_PAYLOAD_HEADER(1, 31));
}

/* TC-PL-05: a buffer one byte short is refused and left untouched. */
static void test_encode_buffer_too_small(void)
{
    node_payload_heartbeat_t   hb = {.counter = 7u};
    node_payload_measurement_t me = {0};
    node_payload_status_t      st = {0};
    uint8_t                    buf[NODE_PAYLOAD_MAX_LEN];

    memset(buf, 0xA5, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode_heartbeat(&hb, buf, NODE_PAYLOAD_HEARTBEAT_LEN - 1u));
    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode_measurement(&me, buf, NODE_PAYLOAD_MEASUREMENT_LEN - 1u));
    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode_status(&st, buf, NODE_PAYLOAD_STATUS_LEN - 1u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(3u, node_payload_encode_heartbeat(&hb, buf, NODE_PAYLOAD_HEARTBEAT_LEN));
    TEST_ASSERT_EQUAL_size_t(9u, node_payload_encode_measurement(&me, buf, NODE_PAYLOAD_MEASUREMENT_LEN));
    TEST_ASSERT_EQUAL_size_t(10u, node_payload_encode_status(&st, buf, NODE_PAYLOAD_STATUS_LEN));
}

/* TC-PL-06: NULL arguments and an unknown type are refused. */
static void test_null_and_bad_type(void)
{
    uint8_t            buf[NODE_PAYLOAD_MAX_LEN];
    node_payload_msg_t m;
    memset(&m, 0, sizeof(m));

    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode_heartbeat(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode_measurement(&m.u.measurement, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode(NULL, buf, sizeof(buf)));
    m.type = (node_payload_type_t)3;
    TEST_ASSERT_EQUAL_size_t(0u, node_payload_encode(&m, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_ERR_PARAM, node_payload_decode(buf, 3u, NULL));
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_ERR_PARAM, node_payload_decode(NULL, 3u, &m));
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_ERR_EMPTY, node_payload_decode(NULL, 0u, &m));
}

/* TC-PL-07: reserved flag bits are never sent. */
static void test_encoder_masks_reserved_flags(void)
{
    node_payload_measurement_t me = {.distance_mm = 1u, .flags = 0xFFu};
    uint8_t                    buf[NODE_PAYLOAD_MEASUREMENT_LEN];
    TEST_ASSERT_EQUAL_size_t(9u, node_payload_encode_measurement(&me, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[7]);
}

/* TC-PL-08: sentinels and the extreme temperatures survive encoding. */
static void test_sentinels_and_temperature_extremes(void)
{
    node_payload_measurement_t me = {
        .distance_mm    = NODE_PAYLOAD_DISTANCE_UNKNOWN,
        .battery_mv     = NODE_PAYLOAD_BATTERY_UNKNOWN,
        .temperature_dc = NODE_PAYLOAD_TEMP_UNKNOWN,
        .flags          = NODE_PAYLOAD_FLAG_SENSOR_ERROR,
        .samples        = NODE_PAYLOAD_SAMPLES_UNKNOWN,
    };
    const uint8_t      expect[] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x01, 0xFF};
    uint8_t            buf[NODE_PAYLOAD_MEASUREMENT_LEN];
    node_payload_msg_t m;

    TEST_ASSERT_EQUAL_size_t(9u, node_payload_encode_measurement(&me, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, sizeof(expect));

    const int16_t temps[] = {INT16_MIN, -401, -1, 0, 1, 851, INT16_MAX - 1};
    for (size_t i = 0; i < N_ITEMS(temps); i++) {
        me.temperature_dc = temps[i];
        TEST_ASSERT_EQUAL_size_t(9u, node_payload_encode_measurement(&me, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_OK, node_payload_decode(buf, sizeof(buf), &m));
        TEST_ASSERT_EQUAL_INT16(temps[i], m.u.measurement.temperature_dc);
    }
}

/* TC-PL-09: saturation helpers. */
static void test_saturation_helpers(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, node_payload_sat_u8(0u));
    TEST_ASSERT_EQUAL_UINT8(254u, node_payload_sat_u8(254u));
    TEST_ASSERT_EQUAL_UINT8(255u, node_payload_sat_u8(255u));
    TEST_ASSERT_EQUAL_UINT8(255u, node_payload_sat_u8(256u));
    TEST_ASSERT_EQUAL_UINT8(255u, node_payload_sat_u8(UINT32_MAX));

    TEST_ASSERT_EQUAL_UINT16(0u, node_payload_uptime_h(3599999u));
    TEST_ASSERT_EQUAL_UINT16(1u, node_payload_uptime_h(3600000u));
    TEST_ASSERT_EQUAL_UINT16(65533u, node_payload_uptime_h(65533ull * 3600000u));
    TEST_ASSERT_EQUAL_UINT16(65534u, node_payload_uptime_h(65534ull * 3600000u));
    TEST_ASSERT_EQUAL_UINT16(65534u, node_payload_uptime_h(65535ull * 3600000u)); /* never "unknown" */
    TEST_ASSERT_EQUAL_UINT16(65534u, node_payload_uptime_h(UINT64_MAX));
}

static void check_version(const char *s, bool ok, uint8_t ma, uint8_t mi, uint8_t pa)
{
    uint8_t a = 99u, b = 99u, c = 99u;
    TEST_ASSERT_EQUAL_MESSAGE(ok, node_payload_parse_version(s, &a, &b, &c), s);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ma, a, s);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(mi, b, s);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(pa, c, s);
}

/* TC-PL-10: firmware version strings as produced by the build (AGENTS.md 2.6). */
static void test_parse_version(void)
{
    check_version("v0.1.0", true, 0, 1, 0);
    check_version("0.0.0-dev", true, 0, 0, 0);
    check_version("0.0.0-ci", true, 0, 0, 0);
    check_version("0.0.0-main-1a2b3c4", true, 0, 0, 0);
    check_version("v1.2.3-rc.1", true, 1, 2, 3);
    check_version("1.2.3+abc", true, 1, 2, 3);
    check_version("255.255.255", true, 255, 255, 255);
    check_version("v12.34.56", true, 12, 34, 56);

    check_version("256.0.0", false, 0, 0, 0);
    check_version("1.2", false, 0, 0, 0);
    check_version("1.2.", false, 0, 0, 0);
    check_version("1..3", false, 0, 0, 0);
    check_version("1.2.3x", false, 0, 0, 0);
    check_version("", false, 0, 0, 0);
    check_version("vx.1.0", false, 0, 0, 0);
    check_version("unknown", false, 0, 0, 0);
    TEST_ASSERT_FALSE(node_payload_parse_version(NULL, NULL, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors_decode);
    RUN_TEST(test_vectors_roundtrip);
    RUN_TEST(test_vectors_invalid);
    RUN_TEST(test_sizes_and_headers);
    RUN_TEST(test_encode_buffer_too_small);
    RUN_TEST(test_null_and_bad_type);
    RUN_TEST(test_encoder_masks_reserved_flags);
    RUN_TEST(test_sentinels_and_temperature_extremes);
    RUN_TEST(test_saturation_helpers);
    RUN_TEST(test_parse_version);
    return UNITY_END();
}
