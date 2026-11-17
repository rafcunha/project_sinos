/* L0 - ring buffer (test plan section 2.1). */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "wioe5_ringbuf.h"

#define SZ 16u

static wioe5_ringbuf_t rb;
static uint8_t         storage[SZ];

void setUp(void)
{
    memset(storage, 0, sizeof(storage));
    TEST_ASSERT_TRUE(wioe5_rb_init(&rb, storage, SZ));
}

void tearDown(void) {}

/* TC-RB-00: size must be a non-zero power of two. */
static void test_init_rejects_bad_size(void)
{
    wioe5_ringbuf_t other;
    uint8_t         buf[24];
    TEST_ASSERT_FALSE(wioe5_rb_init(&other, buf, 0u));
    TEST_ASSERT_FALSE(wioe5_rb_init(&other, buf, 24u));
    TEST_ASSERT_TRUE(wioe5_rb_init(&other, buf, 8u));
}

/* TC-RB-01 */
static void test_new_buffer_is_empty(void)
{
    uint8_t b = 0xAA;
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
    TEST_ASSERT_EQUAL_UINT32(0u, wioe5_rb_count(&rb));
    TEST_ASSERT_FALSE(wioe5_rb_pop(&rb, &b));
    TEST_ASSERT_EQUAL_HEX8(0xAA, b);
}

/* TC-RB-02 */
static void test_push_pop_one(void)
{
    uint8_t b = 0;
    TEST_ASSERT_TRUE(wioe5_rb_push(&rb, 0x5Au));
    TEST_ASSERT_FALSE(wioe5_rb_is_empty(&rb));
    TEST_ASSERT_EQUAL_UINT32(1u, wioe5_rb_count(&rb));
    TEST_ASSERT_TRUE(wioe5_rb_pop(&rb, &b));
    TEST_ASSERT_EQUAL_HEX8(0x5A, b);
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
}

/* TC-RB-03: the full size is usable, FIFO order. */
static void test_fill_to_capacity_fifo(void)
{
    for (uint32_t i = 0; i < SZ; i++) {
        TEST_ASSERT_TRUE(wioe5_rb_push(&rb, (uint8_t)(i + 1u)));
    }
    TEST_ASSERT_EQUAL_UINT32(SZ, wioe5_rb_count(&rb));
    for (uint32_t i = 0; i < SZ; i++) {
        uint8_t b = 0;
        TEST_ASSERT_TRUE(wioe5_rb_pop(&rb, &b));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(i + 1u), b);
    }
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
}

/* TC-RB-04 */
static void test_push_when_full_drops_and_counts(void)
{
    for (uint32_t i = 0; i < SZ; i++) {
        TEST_ASSERT_TRUE(wioe5_rb_push(&rb, (uint8_t)i));
    }
    TEST_ASSERT_FALSE(wioe5_rb_push(&rb, 0xEEu));
    TEST_ASSERT_FALSE(wioe5_rb_push(&rb, 0xEEu));
    TEST_ASSERT_EQUAL_UINT32(2u, rb.overflows);
    TEST_ASSERT_EQUAL_UINT32(SZ, wioe5_rb_count(&rb));
    for (uint32_t i = 0; i < SZ; i++) {
        uint8_t b = 0;
        TEST_ASSERT_TRUE(wioe5_rb_pop(&rb, &b));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, b);
    }
}

/* TC-RB-05 */
static void test_wraparound_many_cycles(void)
{
    uint8_t next_in = 0, next_out = 0;
    for (int cycle = 0; cycle < 100; cycle++) {
        for (int k = 0; k < 11; k++) {
            TEST_ASSERT_TRUE(wioe5_rb_push(&rb, next_in++));
        }
        for (int k = 0; k < 11; k++) {
            uint8_t b = 0;
            TEST_ASSERT_TRUE(wioe5_rb_pop(&rb, &b));
            TEST_ASSERT_EQUAL_UINT8(next_out++, b);
        }
    }
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
    TEST_ASSERT_EQUAL_UINT32(0u, rb.overflows);
}

/* TC-RB-06: free-running indices crossing 2^32. */
static void test_index_wrap_at_uint32_max(void)
{
    rb.head = UINT32_MAX - 3u;
    rb.tail = UINT32_MAX - 3u;
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
    for (uint32_t i = 0; i < SZ; i++) {
        TEST_ASSERT_TRUE(wioe5_rb_push(&rb, (uint8_t)(0x80u + i)));
    }
    TEST_ASSERT_EQUAL_UINT32(SZ, wioe5_rb_count(&rb));
    TEST_ASSERT_FALSE(wioe5_rb_push(&rb, 0u));
    for (uint32_t i = 0; i < SZ; i++) {
        uint8_t b = 0;
        TEST_ASSERT_TRUE(wioe5_rb_pop(&rb, &b));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x80u + i), b);
    }
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
}

/* TC-RB-07 */
static void test_clear_discards_content(void)
{
    uint8_t b;
    TEST_ASSERT_TRUE(wioe5_rb_push(&rb, 1u));
    TEST_ASSERT_TRUE(wioe5_rb_push(&rb, 2u));
    wioe5_rb_clear(&rb);
    TEST_ASSERT_TRUE(wioe5_rb_is_empty(&rb));
    TEST_ASSERT_FALSE(wioe5_rb_pop(&rb, &b));
    TEST_ASSERT_TRUE(wioe5_rb_push(&rb, 3u));
    TEST_ASSERT_TRUE(wioe5_rb_pop(&rb, &b));
    TEST_ASSERT_EQUAL_UINT8(3u, b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_bad_size);
    RUN_TEST(test_new_buffer_is_empty);
    RUN_TEST(test_push_pop_one);
    RUN_TEST(test_fill_to_capacity_fifo);
    RUN_TEST(test_push_when_full_drops_and_counts);
    RUN_TEST(test_wraparound_many_cycles);
    RUN_TEST(test_index_wrap_at_uint32_max);
    RUN_TEST(test_clear_discards_content);
    return UNITY_END();
}
