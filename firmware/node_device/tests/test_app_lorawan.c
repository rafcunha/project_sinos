/* Application uplink cycle (app_lorawan) on top of the driver. */
#include <string.h>

#include "app_lorawan.h"
#include "test_common.h"
#include "unity.h"

#define PERIOD_MS 60000u

static rig_t         r;
static app_lorawan_t app;

static void fwd_event(void *user, wioe5_event_t evt)
{
    app_lorawan_on_event((app_lorawan_t *)user, evt);
}

static uint32_t clock_ms(void *ctx)
{
    return ((fake_port_t *)ctx)->now;
}

static size_t one_byte_payload(void *user, uint8_t *buf, size_t cap)
{
    (void)user;
    (void)cap;
    buf[0] = 0xAB;
    return 1u;
}

static void pump(void *ctx)
{
    app_lorawan_process((app_lorawan_t *)ctx);
}

void setUp(void)
{
    rig_prepare(&r, false);
    r.cfg.on_event   = fwd_event;
    r.cfg.on_op_done = NULL;
    r.cfg.user       = &app;
    wioe5_port_t p   = fake_port_iface(&r.fp);
    TEST_ASSERT_EQUAL_INT(WIOE5_OK, wioe5_init(&r.dev, &p, &r.cfg));

    app_lorawan_cfg_t ac;
    app_lorawan_default_cfg(&ac);
    ac.uplink_period_ms = PERIOD_MS;
    app_lorawan_init(&app, &r.dev, &ac, clock_ms, &r.fp, one_byte_payload, NULL);

    sim_init(&r.sim, &r.fp, &r.dev);
    r.sim.process     = pump;
    r.sim.process_ctx = &app;
}

void tearDown(void) {}

static const sim_step_t SEND_CYCLE[] = {
    {"AT+LW=LEN\r\n", {{5, "+LW: LEN, 51", false}}},
    {"AT+MSGHEX=\"AB\"\r\n", {{10, "+MSGHEX: Start", false}, {2000, "+MSGHEX: Done", false}}},
    {"AT+LOWPOWER\r\n", {{5, "+LOWPOWER: SLEEP", false}}},
};

static void run_for(uint32_t ms)
{
    sim_run_until_time(&r.sim, r.fp.now + ms);
}

/* APP-01: cold start, join, first uplink at once, second one a period later. */
static void test_full_cycle(void)
{
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));
    size_t first_len = r.sim.n_steps - 3u;
    sim_add(&r.sim, SCRIPT_WAKE_OK, SCRIPT_WAKE_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));

    run_for(20000u);
    TEST_ASSERT_TRUE(app_lorawan_can_idle(&app)); /* waiting for the period */
    TEST_ASSERT_TRUE(wioe5_can_stop(&r.dev));     /* module asleep */
    run_for(PERIOD_MS + 10000u);

    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->joins_ok);
    TEST_ASSERT_EQUAL_UINT32(2u, app_lorawan_stats(&app)->uplinks_ok);
    uint32_t gap = r.sim.match_time[first_len + 3u] - r.sim.match_time[first_len];
    TEST_ASSERT_TRUE(gap >= PERIOD_MS - 20u);
    TEST_ASSERT_TRUE(gap <= PERIOD_MS + 50u);
}

/* APP-02: join failure -> retry after the minimum backoff. */
static void test_join_retry_backoff(void)
{
    static const sim_step_t join_fail[] = {
        {"AT+JOIN\r\n", {{10, "+JOIN: Start", false}, {7000, "+JOIN: Join failed", false},
                         {5, "+JOIN: Done", false}}},
    };
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, join_fail, 1);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));
    run_for(60000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    size_t   j1  = SCRIPT_SETUP_N;
    uint32_t gap = r.sim.match_time[j1 + 1u] - (r.sim.match_time[j1] + 7015u);
    TEST_ASSERT_UINT32_WITHIN(20u, 15000u, gap);
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->joins_failed);
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->uplinks_ok);
}

/* APP-03: "No band in N ms" longer than the backoff is honoured. */
static void test_join_no_band_waits(void)
{
    static const sim_step_t join_noband[] = {
        {"AT+JOIN\r\n", {{10, "+JOIN: No band in 40000ms", false}}},
    };
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, join_noband, 1);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));
    run_for(80000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    size_t   j1  = SCRIPT_SETUP_N;
    uint32_t gap = r.sim.match_time[j1 + 1u] - (r.sim.match_time[j1] + 10u);
    TEST_ASSERT_UINT32_WITHIN(20u, 40000u, gap);
}

/* APP-04: backoff doubles and is capped. */
static void test_join_backoff_doubles(void)
{
    static const sim_step_t join_fail[] = {
        {"AT+JOIN\r\n", {{10, "+JOIN: Join failed", false}, {5, "+JOIN: Done", false}}},
    };
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, join_fail, 1);
    sim_add(&r.sim, join_fail, 1);
    sim_add(&r.sim, join_fail, 1);
    run_for(15000u + 30000u + 60000u + 5000u);
    TEST_ASSERT_EQUAL_size_t(SCRIPT_SETUP_N + 3u, r.sim.next);
    size_t j = SCRIPT_SETUP_N;
    TEST_ASSERT_UINT32_WITHIN(30u, 15000u, r.sim.match_time[j + 1u] - r.sim.match_time[j] - 15u);
    TEST_ASSERT_UINT32_WITHIN(30u, 30000u, r.sim.match_time[j + 2u] - r.sim.match_time[j + 1u] - 15u);
}

/* APP-05: send answered "Please join network first" -> re-join, then send. */
static void test_rejoin_when_not_joined(void)
{
    static const sim_step_t send_not_joined[] = {
        {"AT+LW=LEN\r\n", {{5, "+LW: LEN, 51", false}}},
        {"AT+MSGHEX=\"AB\"\r\n", {{10, "+MSGHEX: Please join network first", false}}},
    };
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, send_not_joined, SIM_COUNT(send_not_joined));
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));
    run_for(40000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_UINT32(2u, app_lorawan_stats(&app)->joins_ok);
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->uplinks_failed);
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->uplinks_ok);
}

/* APP-06: unresponsive module -> reset -> backoff -> setup again. */
static void test_unresponsive_triggers_reset(void)
{
    static const sim_step_t silent[] = {
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
        {"AT\r\n", {{0, NULL, false}}},
        {"AT+RESET\r\n", {{5, "+RESET: OK", false}}},
        {"AT\r\n", {{5, "+AT: OK", false}}},
    };
    sim_add(&r.sim, silent, SIM_COUNT(silent));
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));
    run_for(40000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->resets);
    TEST_ASSERT_EQUAL_UINT32(1u, app_lorawan_stats(&app)->uplinks_ok);
    /* setup restarted only after the failure backoff */
    uint32_t gap = r.sim.match_time[5] - r.sim.match_time[4];
    TEST_ASSERT_TRUE(gap >= 10000u);
}

/* APP-07: idle time drives a Stop 2 style clock jump; the cycle resumes on time. */
static void test_idle_ms_and_clock_jump(void)
{
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, SCRIPT_JOIN_OK, SCRIPT_JOIN_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));
    size_t first_len = r.sim.n_steps - 3u;
    sim_add(&r.sim, SCRIPT_WAKE_OK, SCRIPT_WAKE_OK_N);
    sim_add(&r.sim, SEND_CYCLE, SIM_COUNT(SEND_CYCLE));

    TEST_ASSERT_EQUAL_UINT32(0u, app_lorawan_idle_ms(&app)); /* busy at start */
    run_for(20000u);
    TEST_ASSERT_TRUE(app_lorawan_can_idle(&app));
    TEST_ASSERT_TRUE(wioe5_can_stop(&r.dev));

    uint32_t idle     = app_lorawan_idle_ms(&app);
    uint32_t expected = PERIOD_MS - (r.fp.now - r.sim.match_time[first_len]);
    TEST_ASSERT_UINT32_WITHIN(20u, expected, idle);

    /* "Stop 2": no ticks run, the clock jumps by the idle time. */
    r.fp.now += idle;
    TEST_ASSERT_EQUAL_UINT32(0u, app_lorawan_idle_ms(&app));
    run_for(10000u);
    TEST_ASSERT_TRUE_MESSAGE(sim_done(&r.sim), sim_status(&r.sim));
    TEST_ASSERT_EQUAL_UINT32(2u, app_lorawan_stats(&app)->uplinks_ok);
}

/* APP-08: idle time during a backoff wait. */
static void test_idle_ms_during_backoff(void)
{
    static const sim_step_t join_fail[] = {
        {"AT+JOIN\r\n", {{10, "+JOIN: Join failed", false}, {5, "+JOIN: Done", false}}},
    };
    sim_add(&r.sim, SCRIPT_SETUP, SCRIPT_SETUP_N);
    sim_add(&r.sim, join_fail, 1);
    run_for(5000u);
    TEST_ASSERT_TRUE(app_lorawan_can_idle(&app));
    uint32_t idle = app_lorawan_idle_ms(&app);
    TEST_ASSERT_TRUE(idle > 9000u && idle <= 15000u);
    TEST_ASSERT_FALSE(wioe5_can_stop(&r.dev)); /* module awake: driver vetoes Stop 2 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_cycle);
    RUN_TEST(test_join_retry_backoff);
    RUN_TEST(test_join_no_band_waits);
    RUN_TEST(test_join_backoff_doubles);
    RUN_TEST(test_rejoin_when_not_joined);
    RUN_TEST(test_unresponsive_triggers_reset);
    RUN_TEST(test_idle_ms_and_clock_jump);
    RUN_TEST(test_idle_ms_during_backoff);
    return UNITY_END();
}
