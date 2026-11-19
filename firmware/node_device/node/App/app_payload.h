/**
 * @file app_payload.h
 * @brief Chooses and builds each uplink (hardware independent).
 *
 * Policy (docs/payload/payload_spec.md, Q2/Q4):
 *  - the first uplink after boot is a `status`, then one every 24 h of uptime;
 *  - the other uplinks are `heartbeat` while there is no sensor driver
 *    (`measurement` replaces it when the ultrasonic sensor exists).
 *
 * Uptime is kept in 64-bit milliseconds from the 32-bit HAL tick: call
 * app_payload_tick() at least once every 49 days (the main loop does it after
 * every wake-up, so at most every 18 h).
 */
#ifndef APP_PAYLOAD_H
#define APP_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "app_lorawan.h"
#include "node_payload.h"

#define APP_PAYLOAD_STATUS_PERIOD_MS (24u * 3600u * 1000u)

typedef struct {
    const app_lorawan_t *app;          /**< source of the status counters */
    uint8_t              fw_major;
    uint8_t              fw_minor;
    uint8_t              fw_patch;
    uint8_t              reset_cause;  /**< node_payload_reset_cause_t     */
    uint64_t             uptime_ms;
    uint32_t             last_tick;
    uint64_t             next_status_ms;
    uint16_t             heartbeat;
} app_payload_t;

/**
 * @param fw_version  e.g. FW_VERSION ("v0.1.0", "0.0.0-dev"); unparsable -> 0.0.0
 * @param reset_cause node_payload_reset_cause_t read at boot
 * @param app         uplink cycle whose stats feed the status message
 * @param now_ms      current tick, the uptime origin
 */
void app_payload_init(app_payload_t *p, const char *fw_version, uint8_t reset_cause,
                      const app_lorawan_t *app, uint32_t now_ms);

/** Advances the uptime with the tick (wrap-safe between two calls). */
void app_payload_tick(app_payload_t *p, uint32_t now_ms);

/** app_payload_fn for app_lorawan_init(); @p user is the app_payload_t. */
size_t app_payload_build(void *user, uint8_t *buf, size_t cap);

#endif /* APP_PAYLOAD_H */
