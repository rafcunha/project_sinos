/**
 * @file app_lorawan.c
 * @brief Node uplink cycle (see app_lorawan.h). No HAL dependency.
 */
#include "app_lorawan.h"

#include <string.h>

enum {
    S_SETUP = 0,
    S_SETTING_UP,
    S_JOIN,
    S_JOINING,
    S_SEND,
    S_SENDING,
    S_SLEEP,
    S_SLEEPING,
    S_PERIOD,
    S_WAIT,
    S_RESET,
    S_RESETTING,
};

static uint32_t now(const app_lorawan_t *a)
{
    return a->now_ms(a->clock_ctx);
}

static void wait_then(app_lorawan_t *a, uint32_t ms, uint8_t next)
{
    a->wait_start = now(a);
    a->wait_ms    = ms;
    a->after_wait = next;
    a->state      = S_WAIT;
}

static void fail(app_lorawan_t *a, wioe5_status_t st)
{
    a->stats.last_error = st;
}

void app_lorawan_default_cfg(app_lorawan_cfg_t *cfg)
{
    cfg->uplink_period_ms    = 15u * 60u * 1000u;
    cfg->fport               = 1u;
    cfg->confirmed           = false;
    cfg->join_backoff_min_ms = 15u * 1000u;
    cfg->join_backoff_max_ms = 30u * 60u * 1000u;
    cfg->fail_backoff_ms     = 10u * 1000u;
}

void app_lorawan_init(app_lorawan_t *app, wioe5_t *dev, const app_lorawan_cfg_t *cfg,
                      uint32_t (*now_ms)(void *ctx), void *clock_ctx, app_payload_fn payload,
                      void *payload_user)
{
    memset(app, 0, sizeof(*app));
    app->dev          = dev;
    app->cfg          = *cfg;
    app->now_ms       = now_ms;
    app->clock_ctx    = clock_ctx;
    app->payload      = payload;
    app->payload_user = payload_user;
    app->join_backoff = cfg->join_backoff_min_ms;
    app->state        = S_SETUP;
}

void app_lorawan_on_event(app_lorawan_t *app, wioe5_event_t evt)
{
    if (evt == WIOE5_EVT_UNRESPONSIVE) {
        app->unresponsive = true;
    }
}

void app_lorawan_on_downlink(app_lorawan_t *app, const wioe5_downlink_t *dl)
{
    (void)dl;
    app->stats.downlinks++;
}

/** Start an operation; on refusal fall back to a full restart. */
static void started(app_lorawan_t *a, wioe5_status_t st, uint8_t busy_state)
{
    if (st == WIOE5_OK) {
        a->state = busy_state;
    } else {
        fail(a, st);
        wait_then(a, a->cfg.fail_backoff_ms, S_SETUP);
    }
}

static void join_failed(app_lorawan_t *a, wioe5_status_t st)
{
    uint32_t wait = a->join_backoff;
    a->stats.joins_failed++;
    fail(a, st);
    if ((st == WIOE5_ERR_NO_BAND) && (wioe5_info(a->dev)->retry_after_ms > wait)) {
        wait = wioe5_info(a->dev)->retry_after_ms;
    }
    a->join_backoff = (a->join_backoff > a->cfg.join_backoff_max_ms / 2u)
                          ? a->cfg.join_backoff_max_ms
                          : a->join_backoff * 2u;
    wait_then(a, wait, S_JOIN);
}

void app_lorawan_process(app_lorawan_t *app)
{
    app_lorawan_t *a = app;
    wioe5_process(a->dev);

    for (int guard = 0; guard < 8; guard++) {
        if (wioe5_is_busy(a->dev)) {
            return;
        }
        if (a->unresponsive && (a->state != S_RESETTING)) {
            a->unresponsive = false;
            a->state        = S_RESET;
        }
        wioe5_status_t st = wioe5_last_result(a->dev);

        switch (a->state) {
        case S_SETUP:
            a->stats.setups++;
            started(a, wioe5_start_setup(a->dev), S_SETTING_UP);
            return;

        case S_SETTING_UP:
            if (st != WIOE5_OK) {
                fail(a, st);
                wait_then(a, a->cfg.fail_backoff_ms, S_SETUP);
                return;
            }
            a->join_backoff = a->cfg.join_backoff_min_ms;
            a->state        = S_JOIN;
            break;

        case S_JOIN:
            started(a, wioe5_start_join(a->dev, false), S_JOINING);
            return;

        case S_JOINING:
            if (st != WIOE5_OK) {
                join_failed(a, st);
                return;
            }
            a->stats.joins_ok++;
            a->join_backoff  = a->cfg.join_backoff_min_ms;
            a->cycle_started = false;
            a->state         = S_SEND;
            break;

        case S_SEND: {
            uint8_t buf[WIOE5_MAX_PAYLOAD];
            size_t  len      = (a->payload != NULL) ? a->payload(a->payload_user, buf, sizeof(buf)) : 0u;
            a->cycle_start   = now(a);
            a->cycle_started = true;
            if (len == 0u) {
                a->state = S_SLEEP;
                break;
            }
            started(a, wioe5_start_send(a->dev, a->cfg.fport, buf, len, a->cfg.confirmed),
                    S_SENDING);
            return;
        }

        case S_SENDING:
            if (st == WIOE5_OK) {
                a->stats.uplinks_ok++;
            } else {
                a->stats.uplinks_failed++;
                fail(a, st);
                if (st == WIOE5_ERR_NOT_JOINED) {
                    a->state = S_JOIN;
                    break;
                }
            }
            a->state = S_SLEEP;
            break;

        case S_SLEEP:
            started(a, wioe5_start_sleep(a->dev), S_SLEEPING);
            return;

        case S_SLEEPING:
            if (st != WIOE5_OK) {
                fail(a, st); /* module stays awake: costs energy, not correctness */
            }
            a->state = S_PERIOD;
            break;

        case S_PERIOD:
            if ((uint32_t)(now(a) - a->cycle_start) < a->cfg.uplink_period_ms) {
                return;
            }
            a->state = S_SEND;
            break;

        case S_WAIT:
            if ((uint32_t)(now(a) - a->wait_start) < a->wait_ms) {
                return;
            }
            a->state = a->after_wait;
            break;

        case S_RESET:
            a->stats.resets++;
            started(a, wioe5_start_reset(a->dev), S_RESETTING);
            return;

        case S_RESETTING:
            a->unresponsive = false; /* raised again only by a new timeout streak */
            if (st != WIOE5_OK) {
                fail(a, st);
            }
            wait_then(a, a->cfg.fail_backoff_ms, S_SETUP);
            return;

        default:
            a->state = S_SETUP;
            break;
        }
    }
}

bool app_lorawan_can_idle(const app_lorawan_t *app)
{
    return !wioe5_is_busy(app->dev) && ((app->state == S_PERIOD) || (app->state == S_WAIT)) &&
           !app->unresponsive;
}

uint32_t app_lorawan_idle_ms(const app_lorawan_t *app)
{
    uint32_t start;
    uint32_t span;
    if (!app_lorawan_can_idle(app)) {
        return 0u;
    }
    if (app->state == S_PERIOD) {
        start = app->cycle_start;
        span  = app->cfg.uplink_period_ms;
    } else {
        start = app->wait_start;
        span  = app->wait_ms;
    }
    uint32_t elapsed = (uint32_t)(now(app) - start);
    return (elapsed >= span) ? 0u : (span - elapsed);
}

const app_lorawan_stats_t *app_lorawan_stats(const app_lorawan_t *app)
{
    return &app->stats;
}
