/**
 * @file node_payload.h
 * @brief Uplink payload codec of the node (docs/payload/payload_spec.md).
 *
 * Byte 0 is the header: bits 7..5 = format version, bits 4..0 = message type.
 * Multi-byte fields are big-endian, fixed-scale integers; each field that the
 * hardware may not measure has an "unknown" sentinel. No HAL, no float, no malloc.
 */
#ifndef NODE_PAYLOAD_H
#define NODE_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** All uplinks go on this LoRaWAN port (spec F1). */
#define NODE_PAYLOAD_FPORT 1u
/** Largest message; fits every AU915 data rate, dwell time on (spec 1). */
#define NODE_PAYLOAD_MAX_LEN 11u

#define NODE_PAYLOAD_HEADER(version, type) \
    ((uint8_t)((((unsigned)(version) & 0x07u) << 5) | ((unsigned)(type) & 0x1Fu)))

/** Message types (header bits 4..0). */
typedef enum {
    NODE_PAYLOAD_HEARTBEAT   = 0,
    NODE_PAYLOAD_MEASUREMENT = 1,
    NODE_PAYLOAD_STATUS      = 2,
} node_payload_type_t;

/** Current format version of every type, and message lengths (header included). */
#define NODE_PAYLOAD_VERSION         0u
#define NODE_PAYLOAD_HEARTBEAT_LEN   3u
#define NODE_PAYLOAD_MEASUREMENT_LEN 9u
#define NODE_PAYLOAD_STATUS_LEN      10u

/* ---- "Unknown" sentinels (spec F5) ---------------------------------------- */
#define NODE_PAYLOAD_DISTANCE_UNKNOWN 0xFFFFu
#define NODE_PAYLOAD_BATTERY_UNKNOWN  0xFFFFu
#define NODE_PAYLOAD_TEMP_UNKNOWN     INT16_MAX /* 0x7FFF */
#define NODE_PAYLOAD_SAMPLES_UNKNOWN  0xFFu
#define NODE_PAYLOAD_UPTIME_UNKNOWN   0xFFFFu

/* ---- measurement.flags (spec 4.4) ----------------------------------------- */
#define NODE_PAYLOAD_FLAG_SENSOR_ERROR       (1u << 0)
#define NODE_PAYLOAD_FLAG_UNSTABLE           (1u << 1)
#define NODE_PAYLOAD_FLAG_TEMP_UNCOMPENSATED (1u << 2)
#define NODE_PAYLOAD_FLAG_BATTERY_LOW        (1u << 3)
#define NODE_PAYLOAD_FLAG_FIRST_AFTER_BOOT   (1u << 4)
/** Bits 5..7 are reserved: the encoder sends them as 0. */
#define NODE_PAYLOAD_FLAGS_MASK 0x1Fu

/** status.reset_cause (spec 4.5). */
typedef enum {
    NODE_PAYLOAD_RESET_UNKNOWN   = 0,
    NODE_PAYLOAD_RESET_POWER_ON  = 1, /**< power-on / brown-out */
    NODE_PAYLOAD_RESET_PIN       = 2, /**< NRST pin             */
    NODE_PAYLOAD_RESET_SOFTWARE  = 3,
    NODE_PAYLOAD_RESET_IWDG      = 4,
    NODE_PAYLOAD_RESET_WWDG      = 5,
    NODE_PAYLOAD_RESET_LOW_POWER = 6,
    NODE_PAYLOAD_RESET_OTHER     = 7,
} node_payload_reset_cause_t;

typedef struct {
    uint16_t counter;
} node_payload_heartbeat_t;

typedef struct {
    uint16_t distance_mm;    /**< sensor -> water, mm; NODE_PAYLOAD_DISTANCE_UNKNOWN */
    uint16_t battery_mv;     /**< mV; NODE_PAYLOAD_BATTERY_UNKNOWN                   */
    int16_t  temperature_dc; /**< 0.1 degC; NODE_PAYLOAD_TEMP_UNKNOWN                */
    uint8_t  flags;          /**< NODE_PAYLOAD_FLAG_*                                */
    uint8_t  samples;        /**< valid readings used; NODE_PAYLOAD_SAMPLES_UNKNOWN  */
} node_payload_measurement_t;

typedef struct {
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  fw_patch;
    uint8_t  reset_cause;     /**< node_payload_reset_cause_t        */
    uint16_t uptime_h;        /**< hours since boot; ..._UPTIME_UNKNOWN */
    uint8_t  join_failures;   /**< since boot, saturating            */
    uint8_t  uplink_failures; /**< since boot, saturating            */
    uint8_t  module_resets;   /**< since boot, saturating            */
} node_payload_status_t;

/** A decoded (or to-be-encoded) message. */
typedef struct {
    node_payload_type_t type;
    uint8_t             version;
    union {
        node_payload_heartbeat_t   heartbeat;
        node_payload_measurement_t measurement;
        node_payload_status_t      status;
    } u;
} node_payload_msg_t;

typedef enum {
    NODE_PAYLOAD_OK = 0,
    NODE_PAYLOAD_ERR_PARAM,           /**< NULL pointer                        */
    NODE_PAYLOAD_ERR_EMPTY,           /**< zero-length payload                 */
    NODE_PAYLOAD_ERR_UNKNOWN_TYPE,    /**< reserved type (3..31)               */
    NODE_PAYLOAD_ERR_UNKNOWN_VERSION, /**< known type, unknown format version  */
    NODE_PAYLOAD_ERR_BAD_LENGTH,      /**< length differs from the type's size */
} node_payload_result_t;

/* ---- Encoding: @return bytes written, 0 if @p cap is too small or a NULL -- */

size_t node_payload_encode_heartbeat(const node_payload_heartbeat_t *m, uint8_t *buf, size_t cap);
size_t node_payload_encode_measurement(const node_payload_measurement_t *m, uint8_t *buf,
                                       size_t cap);
size_t node_payload_encode_status(const node_payload_status_t *m, uint8_t *buf, size_t cap);

/** Encodes @p msg according to msg->type (always the current version). */
size_t node_payload_encode(const node_payload_msg_t *msg, uint8_t *buf, size_t cap);

/** Decodes one uplink. @p out is only meaningful on NODE_PAYLOAD_OK. */
node_payload_result_t node_payload_decode(const uint8_t *buf, size_t len, node_payload_msg_t *out);

/* ---- Helpers -------------------------------------------------------------- */

/** Clamps a counter to 0..255 (status counters saturate). */
uint8_t node_payload_sat_u8(uint32_t v);

/** Hours since boot from milliseconds, clamped to 0..65534 (65535 = unknown). */
uint16_t node_payload_uptime_h(uint64_t uptime_ms);

/**
 * Parses "MAJOR.MINOR.PATCH" with an optional leading 'v' and any suffix after
 * the patch number that starts with '-' or '+' (e.g. "v0.1.0", "0.0.0-dev",
 * "1.2.3-rc.1"). Each part must be 0..255.
 * @return true on success; on failure the outputs are set to 0.0.0.
 */
bool node_payload_parse_version(const char *s, uint8_t *major, uint8_t *minor, uint8_t *patch);

#endif /* NODE_PAYLOAD_H */
