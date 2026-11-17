/**
 * @file wioe5_hex.h
 * @brief Hex encoding helpers (layer L2). Pure functions.
 */
#ifndef WIOE5_HEX_H
#define WIOE5_HEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Upper-case hex, no separators, NUL-terminated.
 * @param cap size of @p out; must be at least 2 * n + 1.
 * @return false if it does not fit (out untouched beyond cap).
 */
bool wioe5_hex_encode(const uint8_t *in, size_t n, char *out, size_t cap);

/**
 * Decode @p hex_len characters (either case, no separators).
 * @return false on odd length, invalid character, or more than @p cap bytes.
 */
bool wioe5_hex_decode(const char *hex, size_t hex_len, uint8_t *out, size_t cap,
                      size_t *out_len);

#endif /* WIOE5_HEX_H */
