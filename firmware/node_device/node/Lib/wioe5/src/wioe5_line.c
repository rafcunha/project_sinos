/* STUB: interface only. Implementation pending (development phase). */
#include "wioe5_line.h"

void wioe5_lineasm_init(wioe5_lineasm_t *la, char *storage, size_t max_len)
{
    (void)la; (void)storage; (void)max_len;
}

void wioe5_lineasm_reset(wioe5_lineasm_t *la)
{
    (void)la;
}

wioe5_line_result_t wioe5_lineasm_feed(wioe5_lineasm_t *la, uint8_t byte, char **line_out)
{
    (void)la; (void)byte; (void)line_out;
    return WIOE5_LINE_NONE;
}
