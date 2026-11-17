/* L4 - join operation (test plan section 2.7). */
#include <string.h>

#include "test_common.h"
#include "unity.h"

static rig_t r;

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
}

void tearDown(void) {}

static void run_join(bool force, const sim_step_t *steps, size_t n)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_join(&r.dev, force));
    rig_run(&r, steps, n, 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_JOIN, g_cb.last_op);
}

/* TC-JN-01 */
static void test_join_ok(void)
{
    static const uint8_t addr[] = {0x26, 0x0B, 0x12, 0x34};
    run_join(false, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->joined);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(addr, wioe5_info(&r.dev)->dev_addr, 4);
    TEST_ASSERT_EQUAL_HEX32(0x13u, wioe5_info(&r.dev)->net_id);
}

/* TC-JN-02: "Starting" spelling from the PDF. */
static void test_join_ok_starting_variant(void)
{
    static const sim_step_t s[] = {
        {"AT+JOIN\r\n",
         {{10, "+JOIN: Starting", false},
          {10, "+JOIN: NORMAL", false},
          {5000, "+JOIN: NetID 000024 DevAddr 48:00:00:01", false},
          {5, "+JOIN: Done", false}}},
    };
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->joined);
}

/* TC-JN-03 */
static void test_join_failed_then_done(void)
{
    static const sim_step_t s[] = {
        {"AT+JOIN\r\n",
         {{10, "+JOIN: Start", false},
          {10, "+JOIN: NORMAL", false},
          {7000, "+JOIN: Join failed", false},
          {5, "+JOIN: Done", false}}},
    };
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_JOIN_FAILED, g_cb.last_status);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
    /* finished on "Done", not on the grace timer */
    TEST_ASSERT_TRUE(g_cb.op_done_at - r.sim.last_reply_time <= 2u);
}

/* TC-JN-04 */
static void test_join_failed_without_done(void)
{
    static const sim_step_t s[] = {
        {"AT+JOIN\r\n", {{10, "+JOIN: Start", false}, {7000, "+JOIN: Join failed", false}}},
    };
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_JOIN_FAILED, g_cb.last_status);
    uint32_t waited = g_cb.op_done_at - r.sim.last_reply_time;
    TEST_ASSERT_UINT32_WITHIN(2u, r.cfg.timeouts.join_fail_grace_ms, waited);
}

/* TC-JN-05 */
static void test_joined_already(void)
{
    static const sim_step_t s[] = {{"AT+JOIN\r\n", {{10, "+JOIN: Joined already", false}}}};
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->joined);
}

/* TC-JN-06 */
static void test_no_band(void)
{
    static const sim_step_t s[] = {{"AT+JOIN\r\n", {{10, "+JOIN: No band in 5000ms", false}}}};
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_NO_BAND, g_cb.last_status);
    TEST_ASSERT_EQUAL_UINT32(5000u, wioe5_info(&r.dev)->retry_after_ms);
}

/* TC-JN-07 */
static void test_modem_busy(void)
{
    static const sim_step_t s[] = {{"AT+JOIN\r\n", {{10, "+JOIN: LoRaWAN modem is busy", false}}}};
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_MODEM_BUSY, g_cb.last_status);
}

/* TC-JN-07b */
static void test_at_error(void)
{
    static const sim_step_t s[] = {{"AT+JOIN\r\n", {{10, "+JOIN: ERROR(-12)", false}}}};
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_AT, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT16(-12, wioe5_info(&r.dev)->last_at_error);
}

/* TC-JN-08 */
static void test_silence_times_out(void)
{
    static const sim_step_t s[] = {{"AT+JOIN\r\n", {{0, NULL, false}}}};
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, g_cb.last_status);
    uint32_t limit = r.cfg.timeouts.join_ms + tx_ms(strlen("AT+JOIN\r\n"));
    TEST_ASSERT_EQUAL_UINT32(limit, g_cb.op_done_at - r.sim.match_time[0]);
}

/* TC-JN-09 */
static void test_force_join(void)
{
    static const sim_step_t s[] = {
        {"AT+JOIN=FORCE\r\n",
         {{10, "+JOIN: Start", false},
          {6000, "+JOIN: NetID 000013 DevAddr 26:0B:12:34", false},
          {5, "+JOIN: Done", false}}},
    };
    run_join(true, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-JN-10: join while the module sleeps wakes it first. */
static void test_join_wakes_module(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    rig_run(&r, SCRIPT_SLEEP_OK, SCRIPT_SLEEP_OK_N, 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->sleeping);
    cb_reset();
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_join(&r.dev, false));
    sim_add(&r.sim, SCRIPT_WAKE_OK, SCRIPT_WAKE_OK_N);
    rig_run(&r, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N, 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
    TEST_ASSERT_TRUE(r.sim.match_time[1] - r.sim.match_time[0] >= r.cfg.timeouts.wake_settle_ms);
}

/* TC-JN-11: "Done" without NetID and without failure is unexpected. */
static void test_done_without_netid(void)
{
    static const sim_step_t s[] = {
        {"AT+JOIN\r\n", {{10, "+JOIN: Start", false}, {10, "+JOIN: Done", false}}},
    };
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_UNEXPECTED, g_cb.last_status);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
}

/* TC-JN-12: unknown JOIN lines are tolerated. */
static void test_unknown_join_lines_tolerated(void)
{
    static const sim_step_t s[] = {
        {"AT+JOIN\r\n",
         {{10, "+JOIN: Start", false},
          {10, "+JOIN: Something new in firmware 4.1", false},
          {10, "+LOG: whatever", false},
          {6000, "+JOIN: NetID 000013 DevAddr 26:0B:12:34", false},
          {5, "+JOIN: Done", false}}},
    };
    run_join(false, s, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-JN-13: join success reported outside an operation (URC) updates state. */
static void test_join_urc_outside_operation(void)
{
    static const sim_step_t s[] = {
        {NULL,
         {{10, "+JOIN: NetID 000013 DevAddr 26:0B:12:34", false}, {5, "+JOIN: Done", false}}},
    };
    rig_run(&r, s, 1, 1000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->joined);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_JOINED));
    TEST_ASSERT_EQUAL_INT(0, g_cb.op_done_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_join_ok);
    RUN_TEST(test_join_ok_starting_variant);
    RUN_TEST(test_join_failed_then_done);
    RUN_TEST(test_join_failed_without_done);
    RUN_TEST(test_joined_already);
    RUN_TEST(test_no_band);
    RUN_TEST(test_modem_busy);
    RUN_TEST(test_at_error);
    RUN_TEST(test_silence_times_out);
    RUN_TEST(test_force_join);
    RUN_TEST(test_join_wakes_module);
    RUN_TEST(test_done_without_netid);
    RUN_TEST(test_unknown_join_lines_tolerated);
    RUN_TEST(test_join_urc_outside_operation);
    return UNITY_END();
}
