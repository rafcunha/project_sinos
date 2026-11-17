/* L4 - send operation (test plan section 2.8). */
#include <stdio.h>
#include <string.h>

#include "test_common.h"
#include "unity.h"

static rig_t r;

static const uint8_t PAY[] = {0x01, 0x02};

#define LEN_STEP(n) {"AT+LW=LEN\r\n", {{5, "+LW: LEN, " #n, false}}}
#define MSG_DONE(cmd)                                                                          \
    {cmd, {{10, "+MSGHEX: Start", false}, {2500, "+MSGHEX: Done", false}}}

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup_join(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
}

void tearDown(void) {}

static void run_send(uint8_t port, const uint8_t *d, size_t n, bool confirmed,
                     const sim_step_t *steps, size_t n_steps)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_send(&r.dev, port, d, n, confirmed));
    rig_run(&r, steps, n_steps, 200000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_SEND, g_cb.last_op);
}

/* TC-TX-01 */
static void test_unconfirmed_ok(void)
{
    static const sim_step_t s[] = {LEN_STEP(51), MSG_DONE("AT+MSGHEX=\"0102\"\r\n")};
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_UINT16(51u, wioe5_info(&r.dev)->max_payload);
}

/* TC-TX-02 */
static void test_zero_length(void)
{
    static const sim_step_t s[] = {MSG_DONE("AT+MSGHEX\r\n")};
    run_send(1, NULL, 0, false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-TX-03 */
static void test_port_change_only_when_needed(void)
{
    static const sim_step_t s1[] = {
        {"AT+PORT=8\r\n", {{20, "+PORT: 8", false}}},
        LEN_STEP(51),
        MSG_DONE("AT+MSGHEX=\"0102\"\r\n"),
    };
    static const sim_step_t s2[] = {LEN_STEP(51), MSG_DONE("AT+MSGHEX=\"0102\"\r\n")};
    run_send(8, PAY, sizeof(PAY), false, s1, SIM_COUNT(s1));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    cb_reset();
    rig_new_script(&r);
    run_send(8, PAY, sizeof(PAY), false, s2, SIM_COUNT(s2));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-TX-04 */
static void test_confirmed_with_ack(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+CMSGHEX=\"0102\"\r\n",
         {{10, "+CMSGHEX: Start", false},
          {10, "+CMSGHEX: Wait ACK", false},
          {2000, "+CMSGHEX: ACK Received", false},
          {5, "+CMSGHEX: RXWIN1, RSSI -80, SNR 9.0", false},
          {5, "+CMSGHEX: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), true, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT16(-80, wioe5_info(&r.dev)->last_rssi_dbm);
    TEST_ASSERT_EQUAL_INT16(900, wioe5_info(&r.dev)->last_snr_cdb);
    TEST_ASSERT_EQUAL_INT(0, g_cb.dl_count); /* ACK without payload is not a downlink */
}

/* TC-TX-05 */
static void test_confirmed_without_ack(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+CMSGHEX=\"0102\"\r\n",
         {{10, "+CMSGHEX: Start", false},
          {10, "+CMSGHEX: Wait ACK", false},
          {20000, "+CMSGHEX: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), true, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_NO_ACK, g_cb.last_status);
}

/* TC-TX-06 */
static void test_downlink_delivered_before_done(void)
{
    static const uint8_t    expect[] = {0x12, 0x34, 0x56, 0x78};
    static const sim_step_t s[]      = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n",
         {{10, "+MSGHEX: Start", false},
          {1500, "+MSGHEX: PORT: 8; RX: \"12345678\"", false},
          {5, "+MSGHEX: RXWIN1, RSSI -93, SNR 6.25", false},
          {5, "+MSGHEX: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(1, g_cb.dl_count);
    TEST_ASSERT_EQUAL_UINT8(8u, g_cb.dl_port);
    TEST_ASSERT_EQUAL_size_t(4u, g_cb.dl_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, g_cb.dl_data, 4);
    TEST_ASSERT_EQUAL_INT16(-93, g_cb.dl_rssi);
    TEST_ASSERT_EQUAL_INT16(625, g_cb.dl_snr);
    TEST_ASSERT_EQUAL_UINT8(1u, g_cb.dl_window);
    TEST_ASSERT_TRUE(g_cb.dl_seq < g_cb.op_done_seq);
}

/* TC-TX-06b: downlink without RSSI line still delivered, RSSI unknown. */
static void test_downlink_without_rssi(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n",
         {{10, "+MSGHEX: Start", false},
          {1500, "+MSGHEX: PORT: 3; RX: \"AB\"", false},
          {5, "+MSGHEX: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(1, g_cb.dl_count);
    TEST_ASSERT_EQUAL_UINT8(3u, g_cb.dl_port);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, g_cb.dl_rssi);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, g_cb.dl_window);
}

/* TC-TX-07 */
static void test_bad_downlink_hex(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n",
         {{10, "+MSGHEX: Start", false},
          {1500, "+MSGHEX: PORT: 8; RX: \"123\"", false},
          {5, "+MSGHEX: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(0, g_cb.dl_count);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_BAD_DOWNLINK));
}

/* TC-TX-08: LEN 0 -> flush with empty uplink -> LEN ok -> send. */
static void test_len_zero_flush_then_send(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(0),
        MSG_DONE("AT+MSGHEX\r\n"),
        LEN_STEP(50),
        MSG_DONE("AT+MSGHEX=\"0102\"\r\n"),
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-TX-09 */
static void test_len_zero_twice_fails(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(0),
        MSG_DONE("AT+MSGHEX\r\n"),
        LEN_STEP(0),
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_LENGTH, g_cb.last_status);
    TEST_ASSERT_FALSE(fake_tx_contains(&r.fp, "AT+MSGHEX=\"0102\""));
}

/* TC-TX-09b: flush fails -> its error is the result. */
static void test_flush_error_propagates(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(0),
        {"AT+MSGHEX\r\n", {{10, "+MSGHEX: No band in 900ms", false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_NO_BAND, g_cb.last_status);
    TEST_ASSERT_EQUAL_UINT32(900u, wioe5_info(&r.dev)->retry_after_ms);
}

/* TC-TX-10 */
static void test_payload_larger_than_len(void)
{
    static const uint8_t    big[12] = {0};
    static const sim_step_t s[]     = {LEN_STEP(11)};
    run_send(1, big, sizeof(big), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_LENGTH, g_cb.last_status);
    TEST_ASSERT_FALSE(fake_tx_contains(&r.fp, "AT+MSGHEX="));
}

/* TC-TX-11 */
static void test_no_len_check_when_disabled(void)
{
    static const sim_step_t s[] = {MSG_DONE("AT+MSGHEX=\"0102\"\r\n")};
    rig_prepare(&r, false);
    r.cfg.check_len_before_send = false;
    wioe5_port_t p              = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    TEST_ASSERT_TRUE_MESSAGE(rig_setup_join(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-TX-12 */
static void test_not_joined(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n", {{10, "+MSGHEX: Please join network first", false}}},
    };
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->joined);
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_NOT_JOINED, g_cb.last_status);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
}

/* TC-TX-13: error bodies map to status codes. */
typedef struct {
    const char    *line;
    wioe5_status_t status;
} err_case_t;

static void test_error_mapping(void)
{
    static const err_case_t cases[] = {
        {"+MSGHEX: No band in 13469ms", WIOE5_ERR_NO_BAND},
        {"+MSGHEX: No free channel -70", WIOE5_ERR_NO_FREE_CH},
        {"+MSGHEX: DR error", WIOE5_ERR_DR},
        {"+MSGHEX: Length error 5", WIOE5_ERR_LENGTH},
        {"+MSGHEX: LoRaWAN modem is busy", WIOE5_ERR_MODEM_BUSY},
        {"+MSGHEX: ERROR(-12)", WIOE5_ERR_AT},
        {"+MSGHEX: ERROR(-1)", WIOE5_ERR_AT},
    };
    for (size_t i = 0; i < SIM_COUNT(cases); i++) {
        char msg[96];
        snprintf(msg, sizeof(msg), "case %u: %s", (unsigned)i, cases[i].line);
        sim_step_t s[2] = {LEN_STEP(51), {"AT+MSGHEX=\"0102\"\r\n", {{10, NULL, false}}}};
        s[1].reply[0].text = cases[i].line;
        cb_reset();
        rig_new_script(&r);
        TEST_ASSERT_EQUAL_INT_MESSAGE(WIOE5_OK,
                                      wioe5_start_send(&r.dev, 1, PAY, sizeof(PAY), false), msg);
        rig_run(&r, s, 2, 60000u);
        TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(cases[i].status, g_cb.last_status, msg);
        r.fp.now += 100u;
    }
    TEST_ASSERT_EQUAL_INT16(-1, wioe5_info(&r.dev)->last_at_error);
    TEST_ASSERT_EQUAL_UINT32(13469u, wioe5_info(&r.dev)->retry_after_ms);
}

/* TC-TX-14: lines prefixed "+MSG:" are accepted during an MSGHEX send. */
static void test_msg_prefix_accepted(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n",
         {{10, "+MSG: Start", false},
          {1500, "+MSG: RXWIN2, RSSI -106, SNR 4", false},
          {5, "+MSG: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT16(-106, wioe5_info(&r.dev)->last_rssi_dbm);
}

/* TC-TX-15 */
static void test_link_check_answer(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n",
         {{10, "+MSGHEX: Start", false},
          {1500, "+MSGHEX: Link 20, 1", false},
          {5, "+MSGHEX: Done", false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_UINT8(20u, wioe5_info(&r.dev)->link_margin_db);
    TEST_ASSERT_EQUAL_UINT8(1u, wioe5_info(&r.dev)->link_gw_count);
}

/* TC-TX-16 */
static void test_invalid_arguments(void)
{
    static uint8_t big[WIOE5_MAX_PAYLOAD + 1u];
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_start_send(&r.dev, 1, NULL, 1, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_start_send(&r.dev, 1, big, sizeof(big), false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_start_send(&r.dev, 0, PAY, 2, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_start_send(&r.dev, 224, PAY, 2, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_start_send(NULL, 1, PAY, 2, false));
    size_t before = r.fp.tx_len;
    for (int i = 0; i < 100; i++) {
        sim_tick(&r.sim);
    }
    TEST_ASSERT_EQUAL_size_t(before, r.fp.tx_len);
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
}

/* TC-TX-17 / TC-EN-10: maximum payload; timeout budget includes TX time. */
static void test_max_payload(void)
{
    static uint8_t big[WIOE5_MAX_PAYLOAD];
    static char    cmd[WIOE5_CMD_MAX + 8u];
    char          *p = cmd;
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (uint8_t)i;
    }
    p += sprintf(p, "AT+MSGHEX=\"");
    for (size_t i = 0; i < sizeof(big); i++) {
        p += sprintf(p, "%02X", big[i]);
    }
    p += sprintf(p, "\"\r\n");
    size_t cmd_len = (size_t)(p - cmd);
    TEST_ASSERT_TRUE(cmd_len <= WIOE5_CMD_MAX);

    /* Done arrives 10 ms before the deadline measured from the write call. */
    uint32_t   done_at = r.cfg.timeouts.msg_ms + tx_ms(cmd_len) - 10u;
    sim_step_t s[2]    = {LEN_STEP(242), {cmd, {{5, "+MSGHEX: Start", false}, {0, NULL, false}}}};
    s[1].reply[1]      = (sim_reply_t){done_at - 5u, "+MSGHEX: Done", false};
    run_send(1, big, sizeof(big), false, s, 2);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-TX-18: confirmed timeout = cmsg_ms * retry + cmsg_gap_ms * (retry - 1). */
static void test_confirmed_timeout_scales_with_retry(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+CMSGHEX=\"0102\"\r\n", {{0, NULL, false}}},
    };
    run_send(1, PAY, sizeof(PAY), true, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, g_cb.last_status);
    const wioe5_timeouts_t *t     = &r.cfg.timeouts;
    uint32_t                limit = t->cmsg_ms * 3u + t->cmsg_gap_ms * 2u +
                     tx_ms(strlen("AT+CMSGHEX=\"0102\"\r\n"));
    TEST_ASSERT_EQUAL_UINT32(limit, g_cb.op_done_at - r.sim.match_time[1]);
}

/* TC-TX-18b: unconfirmed timeout = msg_ms * rept. */
static void test_unconfirmed_timeout(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n", {{0, NULL, false}}},
    };
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, g_cb.last_status);
    uint32_t limit = r.cfg.timeouts.msg_ms * 1u + tx_ms(strlen("AT+MSGHEX=\"0102\"\r\n"));
    TEST_ASSERT_EQUAL_UINT32(limit, g_cb.op_done_at - r.sim.match_time[1]);
}

/* TC-TX-19: payload is copied at start_send. */
static void test_payload_copied(void)
{
    static const sim_step_t s[] = {LEN_STEP(51), MSG_DONE("AT+MSGHEX=\"0102\"\r\n")};
    uint8_t                 buf[2] = {0x01, 0x02};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_send(&r.dev, 1, buf, sizeof(buf), false));
    buf[0] = 0xEE;
    buf[1] = 0xEE;
    rig_run(&r, s, SIM_COUNT(s), 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-TX-20: a send is allowed without a prior join (the module decides). */
static void test_send_without_join_allowed_by_driver(void)
{
    static const sim_step_t s[] = {
        LEN_STEP(51),
        {"AT+MSGHEX=\"0102\"\r\n", {{10, "+MSGHEX: Please join network first", false}}},
    };
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
    run_send(1, PAY, sizeof(PAY), false, s, SIM_COUNT(s));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_NOT_JOINED, g_cb.last_status);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unconfirmed_ok);
    RUN_TEST(test_zero_length);
    RUN_TEST(test_port_change_only_when_needed);
    RUN_TEST(test_confirmed_with_ack);
    RUN_TEST(test_confirmed_without_ack);
    RUN_TEST(test_downlink_delivered_before_done);
    RUN_TEST(test_downlink_without_rssi);
    RUN_TEST(test_bad_downlink_hex);
    RUN_TEST(test_len_zero_flush_then_send);
    RUN_TEST(test_len_zero_twice_fails);
    RUN_TEST(test_flush_error_propagates);
    RUN_TEST(test_payload_larger_than_len);
    RUN_TEST(test_no_len_check_when_disabled);
    RUN_TEST(test_not_joined);
    RUN_TEST(test_error_mapping);
    RUN_TEST(test_msg_prefix_accepted);
    RUN_TEST(test_link_check_answer);
    RUN_TEST(test_invalid_arguments);
    RUN_TEST(test_max_payload);
    RUN_TEST(test_confirmed_timeout_scales_with_retry);
    RUN_TEST(test_unconfirmed_timeout);
    RUN_TEST(test_payload_copied);
    RUN_TEST(test_send_without_join_allowed_by_driver);
    return UNITY_END();
}
