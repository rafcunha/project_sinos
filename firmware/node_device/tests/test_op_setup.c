/* L4 - setup operation (test plan section 2.6). */
#include <string.h>

#include "test_common.h"
#include "unity.h"

static rig_t r;

void setUp(void) {}
void tearDown(void) {}

static void run_setup(const sim_step_t *steps, size_t n)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_start_setup(&r.dev));
    rig_run(&r, steps, n, 60000u);
}

/* Script = default setup with step @p idx replaced by @p repl, truncated after it
 * when @p stop is true. */
static size_t script_with(sim_step_t *out, size_t idx, const sim_step_t *repl, bool stop)
{
    memcpy(out, SCRIPT_SETUP, SCRIPT_SETUP_N * sizeof(sim_step_t));
    out[idx] = *repl;
    return stop ? idx + 1u : SCRIPT_SETUP_N;
}

/* TC-SU-01 */
static void test_happy_path(void)
{
    static const uint8_t deveui[] = {0x2C, 0xF7, 0xF1, 0x20, 0x42, 0x00, 0x12, 0x34};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(SCRIPT_SETUP, SCRIPT_SETUP_N);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
    TEST_ASSERT_EQUAL_INT(1, g_cb.op_done_count);
    TEST_ASSERT_EQUAL_INT(WIOE5_OP_SETUP, g_cb.last_op);
    TEST_ASSERT_EQUAL_STRING("4.0.11", wioe5_info(&r.dev)->fw_version);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(deveui, wioe5_info(&r.dev)->dev_eui, 8);
    TEST_ASSERT_FALSE(wioe5_info(&r.dev)->joined);
    TEST_ASSERT_FALSE(wioe5_is_busy(&r.dev));
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev)); /* IDLE, module awake */
}

/* TC-SU-02 */
static void test_first_probe_silent_second_ok(void)
{
    sim_step_t steps[SIM_MAX_STEPS];
    steps[0] = (sim_step_t){"AT\r\n", {{0, NULL, false}}};
    memcpy(&steps[1], SCRIPT_SETUP, SCRIPT_SETUP_N * sizeof(sim_step_t));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, SCRIPT_SETUP_N + 1u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-SU-02b: probe answered with an error is retried too. */
static void test_first_probe_error_second_ok(void)
{
    sim_step_t steps[SIM_MAX_STEPS];
    steps[0] = (sim_step_t){"AT\r\n", {{5, "+AT: ERROR(-10)", false}}};
    memcpy(&steps[1], SCRIPT_SETUP, SCRIPT_SETUP_N * sizeof(sim_step_t));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, SCRIPT_SETUP_N + 1u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-SU-03 */
static void test_three_silent_probes_fail(void)
{
    static const sim_step_t silent[] = {
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
    };
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(silent, SIM_COUNT(silent));
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_TIMEOUT, wioe5_last_result(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_join(&r.dev, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_sleep(&r.dev));
}

/* TC-SU-04: optional AT+LOG failing does not abort setup. */
static void test_log_error_is_ignored(void)
{
    sim_step_t       steps[SIM_MAX_STEPS];
    const sim_step_t repl = {"AT+LOG=QUIET\r\n", {{5, "+LOG: ERROR(-10)", false}}};
    size_t           n    = script_with(steps, 1, &repl, false);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, n);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-SU-04b: optional AT+LOG timing out does not abort setup either. */
static void test_log_timeout_is_ignored(void)
{
    sim_step_t       steps[SIM_MAX_STEPS];
    const sim_step_t repl = {"AT+LOG=QUIET\r\n", {{0, NULL, false}}};
    size_t           n    = script_with(steps, 1, &repl, false);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, n);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-SU-05: mandatory step error aborts; nothing else is sent. */
static void test_mode_error_aborts(void)
{
    sim_step_t       steps[SIM_MAX_STEPS];
    const sim_step_t repl = {"AT+MODE=LWOTAA\r\n", {{5, "+MODE: ERROR(-1)", false}}};
    size_t           n    = script_with(steps, 4, &repl, true);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, n);
    for (int i = 0; i < 3000; i++) {
        sim_tick(&r.sim); /* any further TX would fail the script */
    }
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_AT, wioe5_last_result(&r.dev));
    TEST_ASSERT_EQUAL_INT16(-1, wioe5_info(&r.dev)->last_at_error);
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_join(&r.dev, false));
}

/* TC-SU-06 */
static void test_no_channel_mask(void)
{
    sim_step_t steps[SIM_MAX_STEPS];
    memcpy(steps, SCRIPT_SETUP, 6 * sizeof(sim_step_t));
    memcpy(&steps[6], &SCRIPT_SETUP[7], (SCRIPT_SETUP_N - 7u) * sizeof(sim_step_t));
    rig_prepare(&r, false);
    r.cfg.channel_mask = NULL;
    wioe5_port_t p     = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    run_setup(steps, SCRIPT_SETUP_N - 1u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
    TEST_ASSERT_FALSE(fake_tx_contains(&r.fp, "AT+CH"));
}

/* TC-SU-07 */
static void test_adr_off_sets_data_rate(void)
{
    sim_step_t steps[SIM_MAX_STEPS];
    memcpy(steps, SCRIPT_SETUP, 7 * sizeof(sim_step_t));
    steps[7] = (sim_step_t){"AT+ADR=OFF\r\n", {{20, "+ADR: OFF", false}}};
    steps[8] = (sim_step_t){"AT+DR=3\r\n", {{20, "+DR: DR3", false}}};
    memcpy(&steps[9], &SCRIPT_SETUP[8], (SCRIPT_SETUP_N - 8u) * sizeof(sim_step_t));
    rig_prepare(&r, false);
    r.cfg.adr       = false;
    r.cfg.data_rate = 3;
    wioe5_port_t p  = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    run_setup(steps, SCRIPT_SETUP_N + 1u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-SU-08: provisioning writes AppEUI and AppKey; the key never reaches the log. */
static void test_provisioning_and_key_not_logged(void)
{
    static const wioe5_provision_t prov = {
        {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
        {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF,
         0x4F, 0x3C},
    };
    sim_step_t steps[SIM_MAX_STEPS];
    memcpy(steps, SCRIPT_SETUP, SCRIPT_SETUP_N * sizeof(sim_step_t));
    steps[SCRIPT_SETUP_N] = (sim_step_t){"AT+ID=AppEui,\"0011223344556677\"\r\n",
                                         {{20, "+ID: AppEui, 00:11:22:33:44:55:66:77", false}}};
    steps[SCRIPT_SETUP_N + 1u] =
        (sim_step_t){"AT+KEY=APPKEY,\"2B7E151628AED2A6ABF7158809CF4F3C\"\r\n",
                     {{20, "+KEY: APPKEY 2B7E151628AED2A6ABF7158809CF4F3C", false}}};
    rig_prepare(&r, false);
    r.cfg.provision = &prov;
    wioe5_port_t p  = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));
    sim_init(&r.sim, &r.fp, &r.dev);
    run_setup(steps, SCRIPT_SETUP_N + 2u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
    TEST_ASSERT_FALSE_MESSAGE(fake_log_contains(&r.fp, "2B7E151628AED2A6ABF7158809CF4F3C"),
                              "AppKey leaked to the log");
    TEST_ASSERT_FALSE(fake_log_contains(&r.fp, "2B 7E 15 16"));
}

/* TC-SU-09 */
static void test_mode_echo_mismatch(void)
{
    sim_step_t       steps[SIM_MAX_STEPS];
    const sim_step_t repl = {"AT+MODE=LWOTAA\r\n", {{5, "+MODE: LWABP", false}}};
    size_t           n    = script_with(steps, 4, &repl, true);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, n);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_UNEXPECTED, wioe5_last_result(&r.dev));
}

/* TC-SU-09b: numeric echo mismatch (PORT). */
static void test_port_echo_mismatch(void)
{
    sim_step_t       steps[SIM_MAX_STEPS];
    const sim_step_t repl = {"AT+PORT=1\r\n", {{5, "+PORT: 8", false}}};
    size_t           n    = script_with(steps, 8, &repl, true);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, n);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_UNEXPECTED, wioe5_last_result(&r.dev));
}

/* TC-SU-09c: VER/ID answers that cannot be parsed. */
static void test_unparseable_deveui(void)
{
    sim_step_t       steps[SIM_MAX_STEPS];
    const sim_step_t repl = {"AT+ID=DevEui\r\n", {{5, "+ID: DevEui, 2C:F7", false}}};
    size_t           n    = script_with(steps, 3, &repl, true);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    run_setup(steps, n);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_UNEXPECTED, wioe5_last_result(&r.dev));
}

/* TC-SU-10 */
static void test_operations_before_setup(void)
{
    const uint8_t d[] = {1};
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_join(&r.dev, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_send(&r.dev, 1, d, 1, false));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_sleep(&r.dev));
    TEST_ASSERT_EQUAL_INT(WIOE5_ERR_STATE, wioe5_start_wake(&r.dev));
    for (int i = 0; i < 100; i++) {
        sim_tick(&r.sim);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, r.fp.write_calls);
    TEST_ASSERT_EQUAL_INT(0, g_cb.op_done_count);
}

/* TC-SU-11: with reset pin, setup starts with a controlled reset. */
static void test_setup_with_reset_pin(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, true));
    uint32_t t0 = r.fp.now;
    run_setup(SCRIPT_SETUP, SCRIPT_SETUP_N);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));

    TEST_ASSERT_EQUAL_size_t(2u, r.fp.reset_count);
    TEST_ASSERT_TRUE(r.fp.reset_log[0].asserted);
    TEST_ASSERT_EQUAL_UINT32(t0, r.fp.reset_log[0].t);
    TEST_ASSERT_FALSE(r.fp.reset_log[1].asserted);
    uint32_t held = r.fp.reset_log[1].t - r.fp.reset_log[0].t;
    TEST_ASSERT_TRUE(held >= r.cfg.timeouts.reset_pulse_ms);
    uint32_t boot_wait = r.sim.match_time[0] - r.fp.reset_log[1].t;
    TEST_ASSERT_TRUE(boot_wait >= r.cfg.timeouts.boot_ms);
}

/* TC-SU-12: boot noise during the boot wait is discarded. */
static void test_boot_garbage_discarded(void)
{
    sim_step_t steps[SIM_MAX_STEPS];
    steps[0] = (sim_step_t){NULL,
                            {{15, "\xFF\xFE" "boot banner\r\n", true},
                             {50, "+INFO: junk", false},
                             {100, "+AT: OK", false}}};
    memcpy(&steps[1], SCRIPT_SETUP, SCRIPT_SETUP_N * sizeof(sim_step_t));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, true));
    run_setup(steps, SCRIPT_SETUP_N + 1u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_last_result(&r.dev));
}

/* TC-SU-13: without reset pin, setup starts with the probe right away. */
static void test_setup_without_reset_pin(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    uint32_t t0 = r.fp.now;
    run_setup(SCRIPT_SETUP, SCRIPT_SETUP_N);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_size_t(0u, r.fp.reset_count);
    TEST_ASSERT_UINT32_WITHIN(1u, t0, r.sim.match_time[0]);
}

/* TC-SU-14: setup may be repeated (e.g. after reset) and leaves the driver usable. */
static void test_setup_twice(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, rig_init(&r, false));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    TEST_ASSERT_TRUE_MESSAGE(rig_setup(&r), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_INT(2, g_cb.op_done_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_path);
    RUN_TEST(test_first_probe_silent_second_ok);
    RUN_TEST(test_first_probe_error_second_ok);
    RUN_TEST(test_three_silent_probes_fail);
    RUN_TEST(test_log_error_is_ignored);
    RUN_TEST(test_log_timeout_is_ignored);
    RUN_TEST(test_mode_error_aborts);
    RUN_TEST(test_no_channel_mask);
    RUN_TEST(test_adr_off_sets_data_rate);
    RUN_TEST(test_provisioning_and_key_not_logged);
    RUN_TEST(test_mode_echo_mismatch);
    RUN_TEST(test_port_echo_mismatch);
    RUN_TEST(test_unparseable_deveui);
    RUN_TEST(test_operations_before_setup);
    RUN_TEST(test_setup_with_reset_pin);
    RUN_TEST(test_boot_garbage_discarded);
    RUN_TEST(test_setup_without_reset_pin);
    RUN_TEST(test_setup_twice);
    return UNITY_END();
}
