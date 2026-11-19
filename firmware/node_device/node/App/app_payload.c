/**
 * @file app_payload.c
 * @brief Chooses and builds each uplink (see app_payload.h).
 */
#include "app_payload.h"

#include <string.h>

void app_payload_init(app_payload_t *p, const char *fw_version, uint8_t reset_cause,
                      const app_lorawan_t *app, uint32_t now_ms)
{
    memset(p, 0, sizeof(*p));
    p->app         = app;
    p->reset_cause = reset_cause;
    p->last_tick   = now_ms;
    (void)node_payload_parse_version(fw_version, &p->fw_major, &p->fw_minor, &p->fw_patch);
    /* next_status_ms = 0: the first uplink after boot is a status. */
}

void app_payload_tick(app_payload_t *p, uint32_t now_ms)
{
    p->uptime_ms += (uint32_t)(now_ms - p->last_tick);
    p->last_tick = now_ms;
}

static size_t build_status(app_payload_t *p, uint8_t *buf, size_t cap)
{
    node_payload_status_t s;
    memset(&s, 0, sizeof(s));
    s.fw_major    = p->fw_major;
    s.fw_minor    = p->fw_minor;
    s.fw_patch    = p->fw_patch;
    s.reset_cause = p->reset_cause;
    s.uptime_h    = node_payload_uptime_h(p->uptime_ms);
    if (p->app != NULL) {
        const app_lorawan_stats_t *st = app_lorawan_stats(p->app);
        s.join_failures   = node_payload_sat_u8(st->joins_failed);
        s.uplink_failures = node_payload_sat_u8(st->uplinks_failed);
        s.module_resets   = node_payload_sat_u8(st->resets);
    }
    return node_payload_encode_status(&s, buf, cap);
}

size_t app_payload_build(void *user, uint8_t *buf, size_t cap)
{
    app_payload_t *p = (app_payload_t *)user;
    if (p == NULL) {
        return 0u;
    }

    if (p->uptime_ms >= p->next_status_ms) {
        size_t n = build_status(p, buf, cap);
        if (n > 0u) {
            /* Counted from now, not from the schedule: no burst after a long outage. */
            p->next_status_ms = p->uptime_ms + APP_PAYLOAD_STATUS_PERIOD_MS;
            return n;
        }
    }

    node_payload_heartbeat_t hb;
    hb.counter = (uint16_t)(p->heartbeat + 1u);
    size_t n   = node_payload_encode_heartbeat(&hb, buf, cap);
    if (n > 0u) {
        p->heartbeat = hb.counter;
    }
    return n;
}
