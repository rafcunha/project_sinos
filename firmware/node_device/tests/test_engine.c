/* L3 - init, config defaults and command engine (test plan section 2.5). */
#include <string.h>

#include "test_common.h"
#include "unity.h"

static rig_t r;

void setUp(void) {}
void tearDown(void) {}

/* ---- Init / config ------------------------------------------------------- */

/* TC-CF-01: defaults of driver spec sections 11 and 13. */
static void test_config_defaults(void)
{
    wioe5_config_t c;
    memset(&c, 0xA5, sizeof(c));
    wioe5_config_default(&c);
    TEST_ASSERT_EQUAL_STRING("AU915", c.band);
    TEST_ASSERT_EQUAL_STRING("8-15,65", c.channel_mask);
    TEST_ASSERT_TRUE(c.adr);
    TEST_ASSERT_EQUAL_UINT8(2u, c.data_rate);
    TEST_ASSERT_EQUAL_UINT8(1u, c.default_port);
    TEST_ASSERT_EQUAL_UINT8(3u, c.retry);
    TEST_ASSERT_EQUAL_UINT8(1u, c.rept);
    TEST_ASSERT_TRUE(c.check_len_before_send);
    TEST_ASSERT_NULL(c.provision);
    TEST_ASSERT_EQUAL_HEX8(0xFF, c.wake_byte);
    TEST_ASSERT_EQUAL_UINT32(10u, c.timeouts.guard_ms);
    TEST_ASSERT_EQUAL_UINT32(1000u, c.timeouts.cmd_ms);
    TEST_ASSERT_EQUAL_UINT32(2000u, c.timeouts.cmd_eeprom_ms);
    TEST_ASSERT_EQUAL_UINT32(20000u, c.timeouts.join_ms);
    TEST_ASSERT_EQUAL_UINT32(2000u, c.timeouts.join_fail_grace_ms);
    TEST_ASSERT_EQUAL_UINT32(15000u, c.timeouts.msg_ms);
    TEST_ASSERT_EQUAL_UINT32(15000u, c.timeouts.cmsg_ms);
    TEST_ASSERT_EQUAL_UINT32(10000u, c.timeouts.cmsg_gap_ms);
    TEST_ASSERT_EQUAL_UINT32(1000u, c.timeouts.lowpower_ms);
    TEST_ASSERT_EQUAL_UINT32(20u, c.timeouts.wake_settle_ms);
    TEST_ASSERT_EQUAL_UINT32(10u, c.timeouts.reset_pulse_ms);
    TEST_ASSERT_EQUAL_UINT32(2000u, c.timeouts.boot_ms);
    TEST_ASSERT_NULL(c.on_op_done);
    TEST_ASSERT_NULL(c.on_downlink);
    TEST_ASSERT_NULL(c.on_event);
}

/* TC-CF-02 */
static void test_init_rejects_bad_args(void)
{
    rig_prepare(&r, false);
    wioe5_port_t p = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(NULL, &p, &r.cfg));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, NULL, &r.cfg));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, NULL));
    p.write = NULL;
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, &r.cfg));
    p        = fake_port_iface(&r.fp);
    p.now_ms = NULL;
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, &r.cfg));
    p = fake_port_iface(&r.fp);
    r.cfg.band = NULL;
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, &r.cfg));
    wioe5_config_default(&r.cfg);
    r.cfg.default_port = 0;
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, &r.cfg));
    r.cfg.default_port = 224;
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, &r.cfg));
    wioe5_config_default(&r.cfg);
    r.cfg.rept = 0;
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_PARAM, wioe5_init(&r.dev, &p, &r.cfg));
}

/* TC-CF-03: init does no I/O, leaves the driver idle and not set up. */
static void test_init_no_io(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, true));
    TEST_ASSERT_EQUAL_UINT32(0u, r.fp.write_calls);
    TEST_ASSERT_EQUAL_size_t(0u, r.fp.reset_count);
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
    for (int i = 0; i < 100; i++) {
        sim_tick(&r.sim);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, r.fp.write_calls);
}

/* TC-CF-04: start_* only queues; the first byte goes out on process(). */
static void test_start_only_queues(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    TEST_ASSERT_TRUE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_UINT32(0u, r.fp.write_calls);
    wioe5_process(&r.dev);
    TEST_ASSERT_EQUAL_size_t(4u, r.fp.tx_len);
    TEST_ASSERT_EQUAL_MEMORY("AT\r\n", r.fp.tx, 4);
}

/* ---- Engine -------------------------------------------------------------- */

/* TC-EN-01 */
static void test_command_answered_in_time(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_MEMORY("AT\r\n", r.fp.tx, 4);
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_SETUP, g_cb.last_op);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
    TEST_ASSERT_EQUAL_UINT32(0u, wioe5_info(&r.dev)->consecutive_timeouts);
}

/* TC-EN-02 / TC-EN-03: timeout fires exactly at limit (+ TX time). */
static void test_timeout_boundary(void)
{
    static const sim_step_t silent_sleep[] = {{"AT+LOWPOWER\r\n", {{0, NULL, false}}}};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
    sim_add(&r.sim, silent_sleep, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    for (int i = 0; i < 100 && r.sim.next == 0; i++) {
        sim_tick(&r.sim);
    }
    TEST_ASSERT_EQUAL_size_t(1u, r.sim.next);
    uint32_t t_tx  = r.sim.match_time[0];
    uint32_t limit = r.cfg.timeouts.lowpower_ms + tx_ms(strlen("AT+LOWPOWER\r\n"));

    sim_run_until_time(&r.sim, t_tx + limit); /* processed up to t_tx + limit - 1 */
    TEST_ASSERT_TRUE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(0, g_cb.op_done_count);

    sim_tick(&r.sim); /* process at t_tx + limit */
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_SLEEP, g_cb.last_op);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, wioe5_last_result(&r.dev));
    TEST_ASSERT_EQUAL_UINT32(1u, wioe5_info(&r.dev)->consecutive_timeouts);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->sleeping);
}

/* TC-EN-04: three probe timeouts -> one UNRESPONSIVE event. */
static void test_unresponsive_event_once(void)
{
    static const sim_step_t silent[] = {
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
    };
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, silent, SIM_COUNT(silent), 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, wioe5_last_result(&r.dev));
    TEST_ASSERT_EQUAL_UINT32(3u, wioe5_info(&r.dev)->consecutive_timeouts);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_UNRESPONSIVE));

    /* More timeouts without any answer do not repeat the event. */
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, silent, SIM_COUNT(silent), 10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_UINT32(6u, wioe5_info(&r.dev)->consecutive_timeouts);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_UNRESPONSIVE));

    /* After an answer, a new streak reports again. */
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, silent, SIM_COUNT(silent), 10000u);
    TEST_ASSERT_EQUAL_INT(2, cb_event_count(WIOE5_EVT_UNRESPONSIVE));
}

/* TC-EN-05: any answer resets the timeout counter. */
static void test_answer_resets_timeout_counter(void)
{
    static const sim_step_t silent[] = {
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
    };
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, silent, SIM_COUNT(silent), 10000u);
    TEST_ASSERT_EQUAL_UINT32(3u, wioe5_info(&r.dev)->consecutive_timeouts);
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_UINT32(0u, wioe5_info(&r.dev)->consecutive_timeouts);
}

/* TC-EN-06: deadlines survive the 32-bit clock wrap. */
static void test_timeout_across_clock_wrap(void)
{
    sim_step_t steps[SIM_MAX_STEPS];
    steps[0] = (sim_step_t){"AT\r\n", {{0, NULL, false}}};
    memcpy(&steps[1], SCRIPT_SETUP, SCRIPT_SETUP_N * sizeof(sim_step_t));

    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    r.fp.now = UINT32_MAX - 200u;
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, steps, SCRIPT_SETUP_N + 1u, 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
    uint32_t gap   = r.sim.match_time[1] - r.sim.match_time[0];
    uint32_t limit = r.cfg.timeouts.cmd_ms + tx_ms(4);
    TEST_ASSERT_UINT32_WITHIN(2u, limit + 1u, gap);
}

/* TC-EN-07: a line for another command does not end the current one. */
static void test_foreign_line_ignored(void)
{
    static const sim_step_t sleep_noisy[] = {
        {"AT+LOWPOWER\r\n", {{5, "+VER: 4.0.11", false}, {5, "+LOWPOWER: SLEEP", false}}},
    };
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    rig_run(&r, sleep_noisy, 1, 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-EN-08: a command waits for guard_ms of RX silence. */
static void test_guard_time_before_command(void)
{
    static const sim_step_t noisy[] = {
        {NULL,
         {{0, "+X: 1", false},
          {5, "+X: 2", false},
          {5, "+X: 3", false},
          {5, "+X: 4", false},
          {5, "+X: 5", false},
          {5, "+X: 6", false}}},
        {"AT+LOWPOWER\r\n", {{5, "+LOWPOWER: SLEEP", false}}},
    };
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    rig_new_script(&r);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    rig_run(&r, noisy, SIM_COUNT(noisy), 5000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    uint32_t last_noise = r.sim.match_time[0] + 25u;
    TEST_ASSERT_TRUE(r.sim.match_time[1] - last_noise >= r.cfg.timeouts.guard_ms);
}

/* TC-EN-09 */
static void test_write_failure(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    r.fp.write_result = -1;
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    for (int i = 0; i < 5; i++) {
        sim_tick(&r.sim);
    }
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_UART, g_cb.last_status);
}

/* TC-EN-11 */
static void test_busy_rejects_second_operation(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_BUSY, wioe5_start_setup(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_BUSY, wioe5_start_reset(&r.dev));
    rig_run(&r, SCRIPT_SETUP, SCRIPT_SETUP_N, 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
}

/* TC-EN-12: starting an operation from on_op_done is allowed. */
static wioe5_status_t chained_status;
static void           chain_join(void *user, wioe5_op_t op, wioe5_status_t st)
{
    rig_t *rr = (rig_t *)user;
    g_cb.op_done_count++;
    g_cb.last_op     = op;
    g_cb.last_status = st;
    if (op == WIOE5_OP_SETUP && st == WIOE5_OK) {
        chained_status = wioe5_start_join(&rr->dev, false);
    }
}

static void test_chain_from_callback(void)
{
    rig_prepare(&r, false);
    r.cfg.on_op_done = chain_join;
    wioe5_port_t p   = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    chained_status = WIOE5_ERR_STATE;
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    (void)sim_run_until_idle(&r.sim, 60000u);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, chained_status);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(2, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_JOIN, g_cb.last_op);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, g_cb.last_status);
}

/* TC-EN-13: ring buffer overflow during a command. */
static void test_rx_overflow_aborts_operation(void)
{
    static const sim_step_t silent_sleep[] = {{"AT+LOWPOWER\r\n", {{0, NULL, false}}}};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
    sim_add(&r.sim, silent_sleep, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    for (int i = 0; i < 100 && r.sim.next == 0; i++) {
        sim_tick(&r.sim);
    }
    for (uint32_t i = 0; i < WIOE5_RX_RING_SIZE + 50u; i++) {
        wioe5_rx_isr(&r.dev, 'x');
    }
    sim_tick(&r.sim);
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_OVERFLOW, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_RX_OVERFLOW));
    TEST_ASSERT_TRUE(wioe5_info(&r.dev)->rx_overflows >= 50u);
}

/* TC-EN-13b: line overflow during a command. */
static void test_line_overflow_aborts_operation(void)
{
    static const sim_step_t silent_sleep[] = {{"AT+LOWPOWER\r\n", {{0, NULL, false}}}};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    cb_reset();
    rig_new_script(&r);
    sim_add(&r.sim, silent_sleep, 1);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    for (int i = 0; i < 100 && r.sim.next == 0; i++) {
        sim_tick(&r.sim);
    }
    /* Feed in chunks smaller than the ring so only the line overflows. */
    wioe5_rx_isr(&r.dev, '+');
    for (uint32_t i = 0; i < WIOE5_LINE_MAX + 10u; i++) {
        wioe5_rx_isr(&r.dev, 'L');
        if ((i % 64u) == 0u) {
            sim_tick(&r.sim);
        }
    }
    sim_tick(&r.sim);
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_OVERFLOW, g_cb.last_status);
    TEST_ASSERT_EQUAL_INT(1, cb_event_count(WIOE5_EVT_LINE_OVERFLOW));
    TEST_ASSERT_EQUAL_UINT32(1u, wioe5_info(&r.dev)->line_overflows);
}

/* TC-EN-14: wioe5_wait() pumps until done and calls the idle hook. */
static int hook_calls;
static void idle_hook(void)
{
    hook_calls++;
    r.fp.now++; /* time passes while the MCU "sleeps" */
}

static void test_wait_helper(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    hook_calls = 0;
    r.fp.now += 100u; /* well past the guard time */
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    /* Reply arrives before wait() starts; wait must process it and return. */
    wioe5_process(&r.dev);
    TEST_ASSERT_TRUE(fake_tx_contains(&r.fp, "AT+LOWPOWER\r\n"));
    const char *reply = "+LOWPOWER: SLEEP\r\n";
    for (size_t i = 0; i < strlen(reply); i++) {
        wioe5_rx_isr(&r.dev, (uint8_t)reply[i]);
    }
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_wait(&r.dev, idle_hook));
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));

    /* With nothing pending, wait returns the last result immediately. */
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_wait(&r.dev, NULL));
}

/* TC-EN-14b: wait() times out through the hook advancing the clock. */
static void test_wait_helper_timeout(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    hook_calls = 0;
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_sleep(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, wioe5_wait(&r.dev, idle_hook));
    TEST_ASSERT_TRUE(hook_calls >= (int)r.cfg.timeouts.lowpower_ms);
}

/* TC-EN-15: callbacks may be NULL. */
static void test_null_callbacks(void)
{
    rig_prepare(&r, false);
    r.cfg.on_op_done  = NULL;
    r.cfg.on_downlink = NULL;
    r.cfg.on_event    = NULL;
    wioe5_port_t p    = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, SCRIPT_SETUP, SCRIPT_SETUP_N, 60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_init_rejects_bad_args);
    RUN_TEST(test_init_no_io);
    RUN_TEST(test_start_only_queues);
    RUN_TEST(test_command_answered_in_time);
    RUN_TEST(test_timeout_boundary);
    RUN_TEST(test_unresponsive_event_once);
    RUN_TEST(test_answer_resets_timeout_counter);
    RUN_TEST(test_timeout_across_clock_wrap);
    RUN_TEST(test_foreign_line_ignored);
    RUN_TEST(test_guard_time_before_command);
    RUN_TEST(test_write_failure);
    RUN_TEST(test_busy_rejects_second_operation);
    RUN_TEST(test_chain_from_callback);
    RUN_TEST(test_rx_overflow_aborts_operation);
    RUN_TEST(test_line_overflow_aborts_operation);
    RUN_TEST(test_wait_helper);
    RUN_TEST(test_wait_helper_timeout);
    RUN_TEST(test_null_callbacks);
    return UNITY_END();
}
