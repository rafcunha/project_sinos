/**
 * @file wioe5_ringbuf.h
 * @brief Single-producer / single-consumer byte ring buffer (layer L0).
 *
 * The producer (UART ISR) only writes @c head, the consumer (main loop) only
 * writes @c tail. Indices run freely and wrap at 2^32; the storage size must be
 * a power of two, and the full size is usable (capacity == size).
 *
 * Internal to the driver; exposed only because wioe5_t embeds it.
 */
#ifndef WIOE5_RINGBUF_H
#define WIOE5_RINGBUF_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t          *buf;
    uint32_t          size;      /**< power of two */
    volatile uint32_t head;      /**< written by producer only */
    volatile uint32_t tail;      /**< written by consumer only */
    volatile uint32_t overflows; /**< bytes dropped because the buffer was full */
} wioe5_ringbuf_t;

/** @return false if @p size is zero or not a power of two. */
bool     wioe5_rb_init(wioe5_ringbuf_t *rb, uint8_t *storage, uint32_t size);
/** Producer side. @return false (and counts an overflow) if full. */
bool     wioe5_rb_push(wioe5_ringbuf_t *rb, uint8_t byte);
/** Consumer side. @return false if empty. */
bool     wioe5_rb_pop(wioe5_ringbuf_t *rb, uint8_t *byte);
uint32_t wioe5_rb_count(const wioe5_ringbuf_t *rb);
bool     wioe5_rb_is_empty(const wioe5_ringbuf_t *rb);
/** Consumer side: discard everything currently stored. */
void     wioe5_rb_clear(wioe5_ringbuf_t *rb);

#endif /* WIOE5_RINGBUF_H */
