/**
 * @file wioe5_line.c
 * @brief Byte stream to response line assembler (driver spec section 9.1).
 */
#include "wioe5_line.h"

static void start_new_line(wioe5_lineasm_t *la)
{
    la->len        = 0u;
    la->started    = false;
    la->had_noise  = false;
    la->discarding = false;
}

void wioe5_lineasm_init(wioe5_lineasm_t *la, char *storage, size_t max_len)
{
    la->buf           = storage;
    la->max_len       = max_len;
    la->unknown_lines = 0u;
    start_new_line(la);
}

void wioe5_lineasm_reset(wioe5_lineasm_t *la)
{
    start_new_line(la);
}

wioe5_line_result_t wioe5_lineasm_feed(wioe5_lineasm_t *la, uint8_t byte, char **line_out)
{
    if ((byte == (uint8_t)'\r') || (byte == 0u)) {
        return WIOE5_LINE_NONE;
    }

    if (byte == (uint8_t)'\n') {
        wioe5_line_result_t res = WIOE5_LINE_NONE;
        if (la->discarding) {
            /* overlong line already reported */
        } else if (la->started) {
            la->buf[la->len] = '\0';
            *line_out        = la->buf;
            res              = WIOE5_LINE_READY;
        } else if (la->had_noise) {
            la->unknown_lines++;
        } else {
            /* empty line */
        }
        start_new_line(la);
        return res;
    }

    if (la->discarding) {
        return WIOE5_LINE_NONE;
    }

    if (!la->started) {
        if (byte == (uint8_t)'+') {
            la->started = true;
            la->buf[0]  = '+';
            la->len     = 1u;
        } else {
            la->had_noise = true;
        }
        return WIOE5_LINE_NONE;
    }

    if (la->len >= la->max_len) {
        la->discarding = true;
        return WIOE5_LINE_OVERFLOW;
    }
    la->buf[la->len] = (char)byte;
    la->len++;
    return WIOE5_LINE_NONE;
}
