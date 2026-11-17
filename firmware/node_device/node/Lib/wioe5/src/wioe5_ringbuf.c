/**
 * @file wioe5_ringbuf.c
 * @brief SPSC byte ring buffer with free-running 32-bit indices.
 */
#include "wioe5_ringbuf.h"

#include <stddef.h>

static bool is_pow2(uint32_t x)
{
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

bool wioe5_rb_init(wioe5_ringbuf_t *rb, uint8_t *storage, uint32_t size)
{
    if ((rb == NULL) || (storage == NULL) || !is_pow2(size)) {
        return false;
    }
    rb->buf       = storage;
    rb->size      = size;
    rb->head      = 0u;
    rb->tail      = 0u;
    rb->overflows = 0u;
    return true;
}

bool wioe5_rb_push(wioe5_ringbuf_t *rb, uint8_t byte)
{
    uint32_t head = rb->head;
    if ((head - rb->tail) >= rb->size) {
        rb->overflows++;
        return false;
    }
    rb->buf[head & (rb->size - 1u)] = byte;
    /* Publish the byte only after it is stored. */
    rb->head = head + 1u;
    return true;
}

bool wioe5_rb_pop(wioe5_ringbuf_t *rb, uint8_t *byte)
{
    uint32_t tail = rb->tail;
    if (tail == rb->head) {
        return false;
    }
    *byte    = rb->buf[tail & (rb->size - 1u)];
    rb->tail = tail + 1u;
    return true;
}

uint32_t wioe5_rb_count(const wioe5_ringbuf_t *rb)
{
    return rb->head - rb->tail;
}

bool wioe5_rb_is_empty(const wioe5_ringbuf_t *rb)
{
    return rb->head == rb->tail;
}

void wioe5_rb_clear(wioe5_ringbuf_t *rb)
{
    rb->tail = rb->head;
}
