/**
 * @file wioe5.h
 * @brief Public API of the Seeed Wio-E5 LoRaWAN AT-modem driver.
 *
 * See firmware/node_device/docs/wioe5/driver_spec.md for the full specification.
 *
 * Usage outline:
 *   wioe5_config_default(&cfg);  ... adjust cfg ...
 *   wioe5_init(&dev, &port, &cfg);
 *   wioe5_start_setup(&dev);           // asynchronous
 *   while (1) { wioe5_process(&dev); } // results arrive through callbacks
 *
 * Threading: every function runs in the main context except wioe5_rx_isr(),
 * which is the only one allowed from an interrupt handler.
 */
#ifndef WIOE5_H
#define WIOE5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wioe5_config.h"
#include "wioe5_line.h"
#include "wioe5_ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Result codes                                                             */
/* ------------------------------------------------------------------------ */

typedef enum {
    WIOE5_OK = 0,
    WIOE5_ERR_PARAM,       /**< invalid API argument                              */
    WIOE5_ERR_BUSY,        /**< another operation is in progress                  */
    WIOE5_ERR_STATE,       /**< operation not allowed in the current state        */
    WIOE5_ERR_UART,        /**< port->write() failed                              */
    WIOE5_ERR_TIMEOUT,     /**< no terminal response in time                      */
    WIOE5_ERR_AT,          /**< module answered ERROR(n); see info.last_at_error  */
    WIOE5_ERR_UNEXPECTED,  /**< right prefix, unexpected content                  */
    WIOE5_ERR_OVERFLOW,    /**< RX ring buffer or line overflow during operation  */
    WIOE5_ERR_MODEM_BUSY,  /**< "LoRaWAN modem is busy"                           */
    WIOE5_ERR_NOT_JOINED,  /**< "Please join network first"                       */
    WIOE5_ERR_JOIN_FAILED, /**< "Join failed"                                     */
    WIOE5_ERR_NO_FREE_CH,  /**< "No free channel"                                 */
    WIOE5_ERR_NO_BAND,     /**< "No band in N ms"; see info.retry_after_ms        */
    WIOE5_ERR_DR,          /**< "DR error"                                        */
    WIOE5_ERR_LENGTH,      /**< payload larger than allowed / "Length error N"    */
    WIOE5_ERR_NO_ACK,      /**< confirmed uplink finished without an ACK          */
} wioe5_status_t;

typedef enum {
    WIOE5_OP_NONE = 0,
    WIOE5_OP_SETUP,
    WIOE5_OP_JOIN,
    WIOE5_OP_SEND,
    WIOE5_OP_SLEEP,
    WIOE5_OP_WAKE,
    WIOE5_OP_RESET,
} wioe5_op_t;

typedef enum {
    WIOE5_EVT_RX_OVERFLOW = 1, /**< bytes were dropped by the RX ring buffer      */
    WIOE5_EVT_LINE_OVERFLOW,   /**< a response line exceeded WIOE5_LINE_MAX       */
    WIOE5_EVT_UNRESPONSIVE,    /**< WIOE5_UNRESPONSIVE_THRESHOLD timeouts in a row */
    WIOE5_EVT_WOKE,            /**< "+LOWPOWER: WAKEUP" received                  */
    WIOE5_EVT_INPUT_TIMEOUT,   /**< "+INFO: Input timeout" received               */
    WIOE5_EVT_BAD_DOWNLINK,    /**< downlink with invalid or oversized hex        */
    WIOE5_EVT_JOINED,          /**< join confirmed outside a join operation       */
} wioe5_event_t;

typedef enum {
    WIOE5_LOG_DEBUG = 0,
    WIOE5_LOG_INFO,
    WIOE5_LOG_WARN,
    WIOE5_LOG_ERROR,
} wioe5_log_level_t;

/* ------------------------------------------------------------------------ */
/* Hardware port                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    /** Send @p len bytes; may be asynchronous (data stays valid until the next
     *  write). @return 0 on success. Mandatory. */
    int (*write)(void *ctx, const uint8_t *data, size_t len);
    /** Monotonic milliseconds; wraps at 2^32. Mandatory. */
    uint32_t (*now_ms)(void *ctx);
    /** Optional: true holds the module in reset (RST low). */
    void (*set_reset)(void *ctx, bool asserted);
    /** Optional: one diagnostic line per call, no trailing newline. */
    void (*log)(void *ctx, wioe5_log_level_t level, const char *msg);
    void *ctx;
} wioe5_port_t;

/* ------------------------------------------------------------------------ */
/* Configuration                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint32_t guard_ms;           /**< RX silence required before each command   */
    uint32_t cmd_ms;             /**< simple commands                           */
    uint32_t cmd_eeprom_ms;      /**< commands that write the module EEPROM     */
    uint32_t join_ms;            /**< AT+JOIN                                   */
    uint32_t join_fail_grace_ms; /**< wait for "Done" after "Join failed"       */
    uint32_t msg_ms;             /**< unconfirmed uplink, per repetition        */
    uint32_t cmsg_ms;            /**< confirmed uplink, per try                 */
    uint32_t cmsg_gap_ms;        /**< confirmed uplink, gap between tries       */
    uint32_t lowpower_ms;        /**< AT+LOWPOWER                               */
    uint32_t wake_settle_ms;     /**< after the wake byte                       */
    uint32_t reset_pulse_ms;     /**< RST held low                              */
    uint32_t boot_ms;            /**< module boot after reset                   */
} wioe5_timeouts_t;

typedef struct {
    uint8_t app_eui[8];  /**< JoinEUI, big endian */
    uint8_t app_key[16];
} wioe5_provision_t;

typedef struct {
    uint8_t        port;
    const uint8_t *data;      /**< valid only during the callback */
    size_t         len;
    int16_t        rssi_dbm;  /**< INT16_MIN when not reported    */
    int16_t        snr_cdb;   /**< centi-dB (6.25 dB -> 625)      */
    uint8_t        rx_window; /**< 1, 2; 0 = class C, 3 = ping slot; 0xFF unknown */
} wioe5_downlink_t;

typedef void (*wioe5_op_done_cb)(void *user, wioe5_op_t op, wioe5_status_t status);
typedef void (*wioe5_downlink_cb)(void *user, const wioe5_downlink_t *dl);
typedef void (*wioe5_event_cb)(void *user, wioe5_event_t evt);

typedef struct {
    const char              *band;         /**< e.g. "AU915"; must outlive the driver */
    const char              *channel_mask; /**< e.g. "8-15,65"; NULL leaves channels alone */
    bool                     adr;
    uint8_t                  data_rate;    /**< used only when adr == false */
    uint8_t                  default_port; /**< 1..223 */
    uint8_t                  retry;        /**< AT+RETRY (confirmed) */
    uint8_t                  rept;         /**< AT+REPT (unconfirmed), >= 1 */
    bool                     check_len_before_send;
    const wioe5_provision_t *provision;    /**< NULL: keys already stored in the module */
    uint8_t                  wake_byte;
    wioe5_timeouts_t         timeouts;
    wioe5_op_done_cb         on_op_done;
    wioe5_downlink_cb        on_downlink;
    wioe5_event_cb           on_event;
    void                    *user;
} wioe5_config_t;

/* ------------------------------------------------------------------------ */
/* Diagnostics                                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
    char     fw_version[16];
    uint8_t  dev_eui[8];
    uint8_t  dev_addr[4];
    uint32_t net_id;
    bool     joined;
    bool     sleeping;
    int16_t  last_rssi_dbm;
    int16_t  last_snr_cdb;
    uint8_t  link_margin_db;
    uint8_t  link_gw_count;
    int16_t  last_at_error;
    uint32_t retry_after_ms;
    uint16_t max_payload;
    uint32_t consecutive_timeouts;
    uint32_t rx_overflows;
    uint32_t line_overflows;
    uint32_t unknown_lines;
} wioe5_info_t;

/* ------------------------------------------------------------------------ */
/* Driver instance (fields are private: use the API)                        */
/* ------------------------------------------------------------------------ */

typedef struct wioe5 {
    wioe5_port_t     port;
    wioe5_config_t   cfg;
    wioe5_info_t     info;

    wioe5_ringbuf_t  rb;
    uint8_t          rb_storage[WIOE5_RX_RING_SIZE];
    wioe5_lineasm_t  la;
    char             line_storage[WIOE5_LINE_MAX + 1u];
    char             tx_buf[WIOE5_TX_BUF_SIZE];
    size_t           tx_len;

    uint8_t          payload[WIOE5_MAX_PAYLOAD];
    size_t           payload_len;
    uint8_t          payload_port;
    bool             payload_confirmed;

    uint8_t          dl_buf[WIOE5_MAX_DOWNLINK];
    size_t           dl_len;
    uint8_t          dl_port;
    bool             dl_pending;

    uint8_t          state;        /* driver state (private enum)      */
    uint8_t          op;           /* wioe5_op_t in progress           */
    uint8_t          phase;        /* step within the operation        */
    uint8_t          sub;          /* sub-step / per-step flags        */
    uint8_t          attempts;
    bool             force_join;
    bool             setup_done;
    bool             module_port_known;
    uint8_t          module_port;
    bool             cmd_in_flight;
    bool             acked;
    bool             netid_seen;
    bool             join_failed;
    bool             unresponsive_reported;
    bool             rx_seen;
    uint32_t         last_rx_ms;
    uint32_t         t_mark;       /* start of current wait            */
    uint32_t         t_limit;      /* length of current wait           */
    uint32_t         seen_rx_overflows;
    wioe5_status_t   result;
} wioe5_t;

/* ------------------------------------------------------------------------ */
/* API                                                                      */
/* ------------------------------------------------------------------------ */

/** Fill @p cfg with the defaults of driver spec section 13. */
void wioe5_config_default(wioe5_config_t *cfg);

/** No I/O. Copies @p port and @p cfg. @return WIOE5_ERR_PARAM on NULL/invalid. */
wioe5_status_t wioe5_init(wioe5_t *dev, const wioe5_port_t *port, const wioe5_config_t *cfg);

/** Main-loop pump: drains RX, advances the current operation, fires callbacks. */
void wioe5_process(wioe5_t *dev);

/** ISR context: hand one received byte to the driver. */
void wioe5_rx_isr(wioe5_t *dev, uint8_t byte);

/* Asynchronous operations. They only queue the work; the first byte goes out
 * on the next wioe5_process(). The final status arrives via cfg.on_op_done. */
wioe5_status_t wioe5_start_setup(wioe5_t *dev);
wioe5_status_t wioe5_start_join(wioe5_t *dev, bool force);
/** The payload is copied; @p data need not outlive the call. */
wioe5_status_t wioe5_start_send(wioe5_t *dev, uint8_t fport, const uint8_t *data, size_t len,
                                bool confirmed);
wioe5_status_t wioe5_start_sleep(wioe5_t *dev);
wioe5_status_t wioe5_start_wake(wioe5_t *dev);
wioe5_status_t wioe5_start_reset(wioe5_t *dev);

bool                wioe5_is_busy(const wioe5_t *dev);
/** True when the MCU may enter Stop 2: idle, RX drained, module asleep or in reset. */
bool                wioe5_can_stop(const wioe5_t *dev);
wioe5_status_t      wioe5_last_result(const wioe5_t *dev);
const wioe5_info_t *wioe5_info(const wioe5_t *dev);

/** Blocking helper: pump until the current operation ends. @p idle_hook may be NULL. */
wioe5_status_t wioe5_wait(wioe5_t *dev, void (*idle_hook)(void));

#ifdef __cplusplus
}
#endif

#endif /* WIOE5_H */
