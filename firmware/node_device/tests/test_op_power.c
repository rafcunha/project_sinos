/* L4 - sleep / wake / reset and Stop 2 integration (test plan section 2.9). */
#include <string.h>

#include "test_common.h"
#include "unity.h"

static rig_t r;

void setUp(void) {}
void tearDown(void) {}

static void up(bool reset_pin)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, reset_pin));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup_join(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
}

static void up_sleeping(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup_join_sleep(&r), sim_status(&r.sim));
    rig_new_script(&r);
}

/* TC-PW-01 */
static void test_sleep_ok(void)
{
    up(false);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    rig_run(&r, SCRIPT_SLEEP_OK, SCRIPT_SLEEP_OK_N, 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_SLEEP, g_cb.last_op);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->sleeping);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->joined); /* sleeping keeps the session */
}

/* TC-PW-02 */
static void test_sleep_timeout_stays_idle(void)
{
    static const sim_step_t s[] = {{"AT+LOWPOWER\r\n", {{0, NULL, false}}}};
    up(false);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    rig_run(&r, s, 1, 5000u);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, g_cb.last_status);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_wake(&r.dev)); /* not sleeping */
}

/* TC-PW-03 */
static void test_wake_sequence(void)
{
    static const sim_step_t s[] = {
        {"\xFF", {{0, NULL, false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    up_sleeping();
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_wake(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_WAKE, g_cb.last_op);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
    TEST_ASSERT_TRUE(r.sim.match_time[1] - r.sim.match_time[0] >= r.cfg.timeouts.wake_settle_ms);
}

/* TC-PW-03b: the configured wake byte is used. */
static void test_custom_wake_byte(void)
{
    static const sim_step_t s[] = {
        {"A", {{0, NULL, false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    rig_prepare(&r, false);
    r.cfg.wake_byte = 'A';
    wioe5_port_t p  = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    TEST_ASSERT_TRUE_MESSAGE(rig_setup_join_sleep(&r), sim_status(&r.sim));
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_wake(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-PW-04 */
static void test_wake_first_probe_error(void)
{
    static const sim_step_t s[] = {
        {"\xFF", {{0, NULL, false}}},
        {"AT\r\n", {{5, "+AT: ERROR(-23)", false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    up_sleeping();
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_wake(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-PW-04b: two failed probes -> wake fails, module still considered asleep. */
static void test_wake_fails(void)
{
    static const sim_step_t s[] = {
        {"\xFF", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
    };
    up_sleeping();
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_wake(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, g_cb.last_status);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->sleeping);
}

/* TC-PW-05: 0x55 + WAKEUP during wake are URCs. */
static void test_wake_with_wakeup_urc(void)
{
    up_sleeping();
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_wake(&r.dev));
    rig_run(&r, SCRIPT_WAKE_OK, SCRIPT_WAKE_OK_N, 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_WOKE));
}

/* TC-PW-06 */
static void test_no_spontaneous_tx_while_sleeping(void)
{
    up_sleeping();
    size_t before = r.fp.tx_len;
    for (int i = 0; i < 5000; i++) {
        sim_tick(&r.sim);
    }
    TEST_ASSERT_EQUAL_size_t(before, r.fp.tx_len);
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->sleeping);
}

/* TC-PW-07: reset with pin. */
static void test_reset_with_pin(void)
{
    static const sim_step_t s[] = {{"AT\r\n", {{5, "+AT: OK", false}}}};
    up(true);
    r.fp.reset_count = 0;
    uint32_t t0      = r.fp.now;
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_reset(&r.dev));
    rig_run(&r, s, 1, 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_RESET, g_cb.last_op);
    TEST_ASSERT_EQUAL_size_t(2u, r.fp.reset_count);
    TEST_ASSERT_TRUE(r.fp.reset_log[0].asserted);
    TEST_ASSERT_UINT32_WITHIN(1u, t0, r.fp.reset_log[0].t);
    TEST_ASSERT_FALSE(r.fp.reset_log[1].asserted);
    TEST_ASSERT_TRUE(r.fp.reset_log[1].t - r.fp.reset_log[0].t >= r.cfg.timeouts.reset_pulse_ms);
    TEST_ASSERT_TRUE(r.sim.match_time[0] - r.fp.reset_log[1].t >= r.cfg.timeouts.boot_ms);
    /* back to UNINIT: joined cleared, setup required again */
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_join(&r.dev, false));
}

/* TC-PW-08: reset without pin uses AT+RESET. */
static void test_reset_without_pin(void)
{
    static const sim_step_t s[] = {
        {"AT+RESET\r\n", {{5, "+RESET: OK", false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    up(false);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_reset(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_TRUE(r.sim.match_time[1] - r.sim.match_time[0] >= r.cfg.timeouts.boot_ms);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_send(&r.dev, 1, (const uint8_t *)"x", 1, false));
}

/* TC-PW-08b: reset without pin while sleeping wakes the module first. */
static void test_reset_without_pin_while_sleeping(void)
{
    static const sim_step_t s[] = {
        {"\xFF", {{0, NULL, false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
        {"AT+RESET\r\n", {{5, "+RESET: OK", false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    up_sleeping();
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_reset(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
}

/* TC-PW-09: noise after reset is ignored. */
static void test_reset_boot_noise(void)
{
    static const sim_step_t s[] = {
        {"AT+RESET\r\n",
         {{5, "+RESET: OK", false}, {30, "\xFF\xFE" "garbage\r\n", true}, {20, "+INFO: x", false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    up(false);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_reset(&r.dev));
    rig_run(&r, s, SIM_COUNT(s), 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-PW-10: can_stop() per state. */
static void test_can_stop_per_state(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE(wioe5_can_stop(&r.dev)); /* UNINIT, idle */
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev)); /* busy */
    rig_run(&r, SCRIPT_SETUP, SCRIPT_SETUP_N, 60000u);
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev)); /* IDLE, module awake */
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev));
    rig_run(&r, SCRIPT_SLEEP_OK, SCRIPT_SLEEP_OK_N, 5000u);
    TEST_ASSERT_TRUE(wioe5_can_stop(&r.dev)); /* SLEEPING */
}

/* TC-PW-11 */
static void test_can_stop_false_with_pending_rx(void)
{
    up_sleeping();
    TEST_ASSERT_TRUE(wioe5_can_stop(&r.dev));
    wioe5_rx_isr(&r.dev, '+');
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev));
}

/* TC-PW-12: a long clock jump (Stop 2) while sleeping causes no false timeout. */
static void test_clock_jump_while_sleeping(void)
{
    static const sim_step_t s[] = {
        {"\xFF", {{0, NULL, false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
        {"AT+LW=LEN\r\n", {{5, "+LW: LEN, 51", false}}},
        {"AT+MSGHEX=\"0102\"\r\n", {{10, "+MSGHEX: Start", false}, {2000, "+MSGHEX: Done", false}}},
    };
    static const uint8_t pay[] = {0x01, 0x02};
    up_sleeping();
    r.fp.now += 3600000u; /* one hour in Stop 2 */
    sim_tick(&r.sim);
    TEST_ASSERT_EQUAL_INT(0, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_send(&r.dev, 1, pay, sizeof(pay), false));
    rig_run(&r, s, SIM_COUNT(s), 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
}

/* TC-PW-13: send while sleeping -> wake, probe, LEN, MSGHEX (order checked by the script). */
static void test_send_while_sleeping_order(void)
{
    static const sim_step_t s[] = {
        {"\xFF", {{0, NULL, false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
        {"AT+LW=LEN\r\n", {{5, "+LW: LEN, 51", false}}},
        {"AT+MSGHEX=\"AA\"\r\n", {{10, "+MSGHEX: Start", false}, {2000, "+MSGHEX: Done", false}}},
    };
    static const uint8_t pay[] = {0xAA};
    up_sleeping();
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_send(&r.dev, 1, pay, sizeof(pay), false));
    rig_run(&r, s, SIM_COUNT(s), 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count); /* one SEND completion, no separate WAKE */
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_SEND, g_cb.last_op);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
}

/* TC-PW-14: spurious WAKEUP while sleeping. */
static void test_spurious_wakeup(void)
{
    static const sim_step_t s[] = {
        {NULL, {{100, "\x55", true}, {15, "+LOWPOWER: WAKEUP", false}}},
    };
    up_sleeping();
    rig_run(&r, s, 1, 1000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_WOKE));
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev)); /* can sleep again */
}

/* TC-PW-15: sleep requires setup; sleep while sleeping is a state error. */
static void test_sleep_state_rules(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_sleep(&r.dev));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup_join_sleep(&r), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_sleep(&r.dev));
}

/* TC-PW-16: reset is allowed before setup (UNINIT). */
static void test_reset_allowed_before_setup(void)
{
    static const sim_step_t s[] = {{"AT\r\n", {{5, "+AT: OK", false}}}};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, true));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_reset(&r.dev));
    rig_run(&r, s, 1, 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sleep_ok);
    RUN_TEST(test_sleep_timeout_stays_idle);
    RUN_TEST(test_wake_sequence);
    RUN_TEST(test_custom_wake_byte);
    RUN_TEST(test_wake_first_probe_error);
    RUN_TEST(test_wake_fails);
    RUN_TEST(test_wake_with_wakeup_urc);
    RUN_TEST(test_no_spontaneous_tx_while_sleeping);
    RUN_TEST(test_reset_with_pin);
    RUN_TEST(test_reset_without_pin);
    RUN_TEST(test_reset_without_pin_while_sleeping);
    RUN_TEST(test_reset_boot_noise);
    RUN_TEST(test_can_stop_per_state);
    RUN_TEST(test_can_stop_false_with_pending_rx);
    RUN_TEST(test_clock_jump_while_sleeping);
    RUN_TEST(test_send_while_sleeping_order);
    RUN_TEST(test_spurious_wakeup);
    RUN_TEST(test_sleep_state_rules);
    RUN_TEST(test_reset_allowed_before_setup);
    return UNITY_END();
}
