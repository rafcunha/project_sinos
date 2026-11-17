/**
 * @file wioe5_line.h
 * @brief Byte stream to response line assembler (layer L1).
 *
 * Rules (driver spec section 9.1):
 *  - A line ends at '\n'. '\r' and NUL bytes are dropped wherever they appear.
 *  - Everything before the first '+' of a line is dropped (wake-up 0x55, 0xFF,
 *    boot noise). A line that ends without any '+' but had other bytes counts
 *    as an unknown line; a line with no bytes at all is ignored silently.
 *  - A line longer than the configured maximum is dropped up to the next '\n';
 *    WIOE5_LINE_OVERFLOW is reported once, when the limit is crossed.
 *
 * Internal to the driver; exposed only because wioe5_t embeds it.
 */
#ifndef WIOE5_LINE_H
#define WIOE5_LINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WIOE5_LINE_NONE = 0, /**< byte consumed, no complete line yet           */
    WIOE5_LINE_READY,    /**< a complete line is available in *line_out     */
    WIOE5_LINE_OVERFLOW, /**< current line exceeded the maximum length      */
} wioe5_line_result_t;

typedef struct {
    char    *buf;
    size_t   max_len;       /**< longest line in characters (buf holds max_len + 1) */
    size_t   len;
    bool     started;       /**< '+' seen on the current line                */
    bool     had_noise;     /**< non-'+' bytes seen before the '+'           */
    bool     discarding;    /**< dropping an overlong line until '\n'        */
    uint32_t unknown_lines;
} wioe5_lineasm_t;

/** @param storage must hold @p max_len + 1 bytes. */
void wioe5_lineasm_init(wioe5_lineasm_t *la, char *storage, size_t max_len);
/** Forget any partial line (keeps counters). */
void wioe5_lineasm_reset(wioe5_lineasm_t *la);
/**
 * Feed one byte. On WIOE5_LINE_READY, @p line_out points to a NUL-terminated
 * line that stays valid (and may be modified by the caller) until the next call.
 */
wioe5_line_result_t wioe5_lineasm_feed(wioe5_lineasm_t *la, uint8_t byte, char **line_out);

#endif /* WIOE5_LINE_H */
