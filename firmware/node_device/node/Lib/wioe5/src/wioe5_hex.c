/**
 * @file wioe5_hex.c
 * @brief Hex encoding helpers.
 */
#include "wioe5_hex.h"

static const char HEX_DIGITS[] = "0123456789ABCDEF";

static int nibble(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    return -1;
}

bool wioe5_hex_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    if ((out == NULL) || (cap < (2u * n) + 1u) || ((in == NULL) && (n > 0u))) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        out[2u * i]      = HEX_DIGITS[(in[i] >> 4) & 0x0Fu];
        out[2u * i + 1u] = HEX_DIGITS[in[i] & 0x0Fu];
    }
    out[2u * n] = '\0';
    return true;
}

bool wioe5_hex_decode(const char *hex, size_t hex_len, uint8_t *out, size_t cap,
                      size_t *out_len)
{
    if ((hex_len % 2u) != 0u) {
        return false;
    }
    size_t n = hex_len / 2u;
    if ((n > cap) || ((n > 0u) && ((hex == NULL) || (out == NULL)))) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        int hi = nibble(hex[2u * i]);
        int lo = nibble(hex[2u * i + 1u]);
        if ((hi < 0) || (lo < 0)) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (out_len != NULL) {
        *out_len = n;
    }
    return true;
}
