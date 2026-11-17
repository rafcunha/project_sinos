/* STUB: interface only. Implementation pending (development phase). */
#include "wioe5_ringbuf.h"

bool wioe5_rb_init(wioe5_ringbuf_t *rb, uint8_t *storage, uint32_t size)
{
    (void)rb; (void)storage; (void)size;
    return false;
}

bool wioe5_rb_push(wioe5_ringbuf_t *rb, uint8_t byte)
{
    (void)rb; (void)byte;
    return false;
}

bool wioe5_rb_pop(wioe5_ringbuf_t *rb, uint8_t *byte)
{
    (void)rb; (void)byte;
    return false;
}

uint32_t wioe5_rb_count(const wioe5_ringbuf_t *rb)
{
    (void)rb;
    return 0u;
}

bool wioe5_rb_is_empty(const wioe5_ringbuf_t *rb)
{
    (void)rb;
    return false;
}

void wioe5_rb_clear(wioe5_ringbuf_t *rb)
{
    (void)rb;
}
