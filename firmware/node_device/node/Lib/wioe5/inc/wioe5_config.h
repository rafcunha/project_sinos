/**
 * @file wioe5_config.h
 * @brief Compile-time sizes for the Wio-E5 driver. Override with -D if needed.
 */
#ifndef WIOE5_CONFIG_H
#define WIOE5_CONFIG_H

/** RX ring buffer size in bytes. Must be a power of two. */
#ifndef WIOE5_RX_RING_SIZE
#define WIOE5_RX_RING_SIZE 512u
#endif

/** Longest response line accepted, in characters (without terminator). */
#ifndef WIOE5_LINE_MAX
#define WIOE5_LINE_MAX 560u
#endif

/** Longest AT command, in characters, including the trailing "\r\n" (PDF section 2.1). */
#define WIOE5_CMD_MAX 528u

/** TX buffer: longest command plus NUL. */
#define WIOE5_TX_BUF_SIZE (WIOE5_CMD_MAX + 1u)

/** Largest uplink payload accepted by wioe5_start_send(). */
#ifndef WIOE5_MAX_PAYLOAD
#define WIOE5_MAX_PAYLOAD 242u
#endif

/** Largest downlink payload the driver can decode. */
#define WIOE5_MAX_DOWNLINK 242u

/** Consecutive timeouts that raise WIOE5_EVT_UNRESPONSIVE. */
#ifndef WIOE5_UNRESPONSIVE_THRESHOLD
#define WIOE5_UNRESPONSIVE_THRESHOLD 3u
#endif

#endif /* WIOE5_CONFIG_H */
