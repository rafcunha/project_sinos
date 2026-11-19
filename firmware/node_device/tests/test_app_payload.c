/* app_payload: which message each uplink carries (payload spec Q2/Q4, test plan 2.12). */
#include <string.h>

#include "app_payload.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define HOUR_MS (3600u * 1000u)

static app_lorawan_t s_app; /* only its stats are read */
static app_payload_t s_p;
static uint8_t       s_buf[NODE_PAYLOAD_MAX_LEN];

static node_payload_msg_t build(void)
{
    node_payload_msg_t m;
    size_t             n = app_payload_build(&s_p, s_buf, sizeof(s_buf));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_OK, node_payload_decode(s_buf, n, &m));
    return m;
}

static void init(const char *ver, uint32_t now)
{
    memset(&s_app, 0, sizeof(s_app));
    app_payload_init(&s_p, ver, NODE_PAYLOAD_RESET_POWER_ON, &s_app, now);
}

/* TC-AP-01: first uplink after boot is a status with version, cause and counters. */
static void test_first_uplink_is_status(void)
{
    init("v1.2.3", 1000u);
    s_app.stats.joins_failed   = 3u;
    s_app.stats.uplinks_failed = 300u; /* saturates */
    s_app.stats.resets         = 1u;
    app_payload_tick(&s_p, 1000u + 2u * HOUR_MS + 59u * 60000u);

    node_payload_msg_t m = build();
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, m.type);
    TEST_ASSERT_EQUAL_UINT8(1u, m.u.status.fw_major);
    TEST_ASSERT_EQUAL_UINT8(2u, m.u.status.fw_minor);
    TEST_ASSERT_EQUAL_UINT8(3u, m.u.status.fw_patch);
    TEST_ASSERT_EQUAL_UINT8(NODE_PAYLOAD_RESET_POWER_ON, m.u.status.reset_cause);
    TEST_ASSERT_EQUAL_UINT16(2u, m.u.status.uptime_h); /* 2 h 59 min -> 2 */
    TEST_ASSERT_EQUAL_UINT8(3u, m.u.status.join_failures);
    TEST_ASSERT_EQUAL_UINT8(255u, m.u.status.uplink_failures);
    TEST_ASSERT_EQUAL_UINT8(1u, m.u.status.module_resets);
}

/* TC-AP-02: then heartbeats counting from 1 until 24 h after the last status. */
static void test_heartbeats_then_status_every_24h(void)
{
    init("0.0.0-dev", 0u);
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, build().type);

    uint32_t now = 0u;
    for (uint16_t i = 1u; i <= 95u; i++) { /* 15 min period: 95 uplinks < 24 h */
        now += 15u * 60000u;
        app_payload_tick(&s_p, now);
        node_payload_msg_t m = build();
        TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_HEARTBEAT, m.type);
        TEST_ASSERT_EQUAL_UINT16(i, m.u.heartbeat.counter);
    }
    now += 15u * 60000u; /* exactly 24 h */
    app_payload_tick(&s_p, now);
    node_payload_msg_t m = build();
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, m.type);
    TEST_ASSERT_EQUAL_UINT16(24u, m.u.status.uptime_h);
    TEST_ASSERT_EQUAL_UINT8(0u, m.u.status.fw_major); /* "0.0.0-dev" -> 0.0.0 */

    now += 15u * 60000u;
    app_payload_tick(&s_p, now);
    m = build();
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_HEARTBEAT, m.type);
    TEST_ASSERT_EQUAL_UINT16(96u, m.u.heartbeat.counter); /* not reset by the status */
}

/* TC-AP-03: after a long silence only one status is sent, the next 24 h later. */
static void test_no_status_burst_after_long_gap(void)
{
    init("v0.1.0", 0u);
    (void)build();                         /* boot status at t = 0 */
    app_payload_tick(&s_p, 72u * HOUR_MS); /* 3 days without uplinks */
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, build().type);
    app_payload_tick(&s_p, 72u * HOUR_MS + 1u);
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_HEARTBEAT, build().type);
    app_payload_tick(&s_p, 96u * HOUR_MS);
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, build().type);
}

/* TC-AP-04: uptime survives the 32-bit tick wrap (49.7 days). */
static void test_uptime_across_tick_wrap(void)
{
    uint32_t now = 0xFFFFFFFFu - HOUR_MS; /* boot one hour before the wrap */
    init("v0.1.0", now);
    for (int i = 0; i < 60; i++) { /* 60 days in 18 h steps (max Stop 2 sleep) */
        now += 18u * HOUR_MS;
        app_payload_tick(&s_p, now);
    }
    s_p.next_status_ms = 0u; /* force a status */
    TEST_ASSERT_EQUAL_UINT16(60u * 18u, build().u.status.uptime_h);
}

/* TC-AP-05: small buffer -> skip the uplink without consuming the counter. */
static void test_small_buffer_skips(void)
{
    init("v0.1.0", 0u);
    TEST_ASSERT_EQUAL_size_t(0u, app_payload_build(&s_p, s_buf, NODE_PAYLOAD_HEARTBEAT_LEN - 1u));
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, build().type); /* status still due */
    TEST_ASSERT_EQUAL_size_t(0u, app_payload_build(&s_p, s_buf, NODE_PAYLOAD_HEARTBEAT_LEN - 1u));
    TEST_ASSERT_EQUAL_UINT16(1u, build().u.heartbeat.counter);
    TEST_ASSERT_EQUAL_size_t(0u, app_payload_build(NULL, s_buf, sizeof(s_buf)));
}

/* TC-AP-06: a status that does not fit falls back to a heartbeat; status stays due. */
static void test_status_does_not_fit_falls_back(void)
{
    init("v0.1.0", 0u);
    TEST_ASSERT_EQUAL_size_t(3u, app_payload_build(&s_p, s_buf, NODE_PAYLOAD_STATUS_LEN - 1u));
    TEST_ASSERT_EQUAL_HEX8(0x00, s_buf[0]);
    TEST_ASSERT_EQUAL_INT(NODE_PAYLOAD_STATUS, build().type);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_uplink_is_status);
    RUN_TEST(test_heartbeats_then_status_every_24h);
    RUN_TEST(test_no_status_burst_after_long_gap);
    RUN_TEST(test_uptime_across_tick_wrap);
    RUN_TEST(test_small_buffer_skips);
    RUN_TEST(test_status_does_not_fit_falls_back);
    return UNITY_END();
}
