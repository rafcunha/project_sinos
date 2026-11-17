/**
 * @file app_lorawan.h
 * @brief Node uplink cycle on top of the Wio-E5 driver (hardware independent).
 *
 * Cycle: setup -> join -> [send -> module sleep -> wait period] forever.
 * Retry policy (kept here, not in the driver, per driver spec D8):
 *  - setup failure or reset: wait fail_backoff_ms, then setup again;
 *  - join failure: exponential backoff join_backoff_min_ms .. join_backoff_max_ms,
 *    never shorter than the module's "No band in N ms";
 *  - "Please join network first" on send: re-join;
 *  - WIOE5_EVT_UNRESPONSIVE: reset the module (hardware pin if wired), then setup.
 *
 * The caller pumps app_lorawan_process() from the main loop and may idle the
 * MCU whenever app_lorawan_can_idle() returns true.
 */
#ifndef APP_LORAWAN_H
#define APP_LORAWAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wioe5.h"

typedef struct {
    uint32_t uplink_period_ms;    /**< time between uplink starts          */
    uint8_t  fport;               /**< LoRaWAN port of the uplinks         */
    bool     confirmed;           /**< confirmed uplinks                   */
    uint32_t join_backoff_min_ms;
    uint32_t join_backoff_max_ms;
    uint32_t fail_backoff_ms;     /**< after setup/reset failure            */
} app_lorawan_cfg_t;

/** Builds the next payload into @p buf. @return length (0 = skip this uplink). */
typedef size_t (*app_payload_fn)(void *user, uint8_t *buf, size_t cap);

typedef struct {
    uint32_t setups;
    uint32_t joins_ok;
    uint32_t joins_failed;
    uint32_t uplinks_ok;
    uint32_t uplinks_failed;
    uint32_t resets;
    uint32_t downlinks;
    wioe5_status_t last_error;
} app_lorawan_stats_t;

typedef struct {
    wioe5_t            *dev;
    app_lorawan_cfg_t   cfg;
    uint32_t          (*now_ms)(void *ctx);
    void               *clock_ctx;
    app_payload_fn      payload;
    void               *payload_user;

    uint8_t             state;
    uint8_t             after_wait;   /* state to enter when the wait ends */
    uint32_t            wait_start;
    uint32_t            wait_ms;
    uint32_t            join_backoff;
    uint32_t            cycle_start;
    bool                cycle_started;
    bool                unresponsive;
    app_lorawan_stats_t stats;
} app_lorawan_t;

void app_lorawan_default_cfg(app_lorawan_cfg_t *cfg);

/** @p dev must already be initialised with wioe5_init(). Starts the cycle. */
void app_lorawan_init(app_lorawan_t *app, wioe5_t *dev, const app_lorawan_cfg_t *cfg,
                      uint32_t (*now_ms)(void *ctx), void *clock_ctx, app_payload_fn payload,
                      void *payload_user);

/** Pump: runs wioe5_process() and advances the cycle. */
void app_lorawan_process(app_lorawan_t *app);

/** Forward the driver's on_event callback here. */
void app_lorawan_on_event(app_lorawan_t *app, wioe5_event_t evt);

/** Forward the driver's on_downlink callback here (counted; not used yet). */
void app_lorawan_on_downlink(app_lorawan_t *app, const wioe5_downlink_t *dl);

/** True while only waiting (period or backoff) and the driver is idle. */
bool app_lorawan_can_idle(const app_lorawan_t *app);

const app_lorawan_stats_t *app_lorawan_stats(const app_lorawan_t *app);

#endif /* APP_LORAWAN_H */
