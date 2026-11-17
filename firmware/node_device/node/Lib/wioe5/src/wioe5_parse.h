/**
 * @file wioe5_parse.h
 * @brief Pure response parsers (layer L2). No state, no I/O.
 *
 * Grammar (driver spec section 9.2):
 *   line := "+" NAME ":" [spaces] BODY
 *   NAME := 1..16 characters [A-Za-z0-9]
 * NAME comparisons are case-insensitive. BODY is trimmed at both ends.
 */
#ifndef WIOE5_PARSE_H
#define WIOE5_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIOE5_NAME_MAX 16u

typedef struct {
    char        name[WIOE5_NAME_MAX + 1u]; /**< as received (original case)    */
    const char *body;                      /**< points into the line, trimmed  */
    bool        is_error;                  /**< body is exactly "ERROR(<int>)" */
    int16_t     error_code;
} wioe5_resp_t;

/**
 * Split a response line. The line is modified in place (body trimming).
 * @return false if the line is not of the form "+NAME:...".
 */
bool wioe5_parse_resp(char *line, wioe5_resp_t *out);

/** Case-insensitive comparison of the response name. */
bool wioe5_resp_name_is(const wioe5_resp_t *r, const char *name);

/* ---- Simple bodies ----------------------------------------------------- */

/** "4.0.11" -> copied into @p out. Fails if empty or does not fit. */
bool wioe5_parse_ver(const char *body, char *out, size_t cap);

/**
 * "<field>, XX:XX:...:XX" with exactly @p n bytes. @p field is matched
 * case-insensitively (e.g. "DevEui", "AppEui", "DevAddr").
 */
bool wioe5_parse_id(const char *body, const char *field, uint8_t *out, size_t n);

/** "LEN, 50" -> 50. */
bool wioe5_parse_lw_len(const char *body, uint16_t *len);

/** Body is a decimal unsigned integer, optionally surrounded by spaces. */
bool wioe5_parse_uint_body(const char *body, uint32_t *value);

/* ---- JOIN / MSG / CMSG / MSGHEX / CMSGHEX bodies ----------------------- */

typedef enum {
    WIOE5_EV_UNKNOWN = 0,
    WIOE5_EV_START,           /* "Start", "Starting"                      */
    WIOE5_EV_DONE,            /* "Done"                                   */
    WIOE5_EV_NORMAL,          /* "NORMAL" (join type)                     */
    WIOE5_EV_NETID,           /* "NetID 000024 DevAddr 48:00:00:01"       */
    WIOE5_EV_JOIN_FAILED,     /* "Join failed"                            */
    WIOE5_EV_JOINED_ALREADY,  /* "Joined already"                         */
    WIOE5_EV_TX,              /* "TX \"...\""                             */
    WIOE5_EV_WAIT_ACK,        /* "Wait ACK"                               */
    WIOE5_EV_FPENDING,        /* "FPENDING"                               */
    WIOE5_EV_ACK,             /* "ACK Received"                           */
    WIOE5_EV_MULTICAST,       /* "MULTICAST"                              */
    WIOE5_EV_LINK,            /* "Link 20, 1"                             */
    WIOE5_EV_DOWNLINK,        /* "PORT: 8; RX: \"12345678\""              */
    WIOE5_EV_RXINFO,          /* "RXWIN1, RSSI -93, SNR 6.25"             */
    WIOE5_EV_ERR_BUSY,        /* "LoRaWAN modem is busy"                  */
    WIOE5_EV_ERR_NOT_JOINED,  /* "Please join network first"              */
    WIOE5_EV_ERR_NO_FREE_CH,  /* "No free channel ..."                    */
    WIOE5_EV_ERR_NO_BAND,     /* "No band in 13469ms"                     */
    WIOE5_EV_ERR_DR,          /* "DR error"                               */
    WIOE5_EV_ERR_LENGTH,      /* "Length error N"                         */
} wioe5_ev_kind_t;

typedef struct {
    wioe5_ev_kind_t kind;
    uint32_t        value;        /* NO_BAND: ms; LENGTH: N                  */
    uint32_t        net_id;       /* NETID                                   */
    uint8_t         dev_addr[4];  /* NETID                                   */
    uint8_t         link_margin;  /* LINK                                    */
    uint8_t         link_gw;      /* LINK                                    */
    uint8_t         port;         /* DOWNLINK                                */
    const char     *hex;          /* DOWNLINK: points into body, not NUL-terminated */
    size_t          hex_len;      /* DOWNLINK                                */
    uint8_t         rx_window;    /* RXINFO                                  */
    int16_t         rssi_dbm;     /* RXINFO                                  */
    int16_t         snr_cdb;      /* RXINFO                                  */
} wioe5_ev_t;

/** Classify a JOIN/MSG-family body. Matching is case-insensitive.
 *  @return false (kind = WIOE5_EV_UNKNOWN) if not recognized or malformed. */
bool wioe5_parse_event(const char *body, wioe5_ev_t *ev);

/** "6.25" -> 625, "-7.5" -> -750, "4" -> 400 (extra decimals truncated). */
bool wioe5_parse_centi(const char *s, size_t len, int16_t *out);

#endif /* WIOE5_PARSE_H */
