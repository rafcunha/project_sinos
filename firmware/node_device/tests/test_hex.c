/* L2 - hex helpers (test plan section 2.4). */
#include <string.h>

#include "unity.h"
#include "wioe5_hex.h"

void setUp(void) {}
void tearDown(void) {}

/* TC-HX-01 */
static void test_encode_uppercase_no_separators(void)
{
    const uint8_t in[] = {0x00, 0xAB, 0xFF};
    char          out[7];
    TEST_ASSERT_TRUE(wioe5_hex_encode(in, sizeof(in), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("00ABFF", out);
}

/* TC-HX-02 */
static void test_encode_empty(void)
{
    char out[1] = {'x'};
    TEST_ASSERT_TRUE(wioe5_hex_encode(NULL, 0u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

/* TC-HX-03 */
static void test_encode_does_not_fit(void)
{
    const uint8_t in[] = {0x12, 0x34};
    char          out[8];
    memset(out, '#', sizeof(out));
    TEST_ASSERT_FALSE(wioe5_hex_encode(in, sizeof(in), out, 4u)); /* needs 5 */
    TEST_ASSERT_EQUAL_CHAR('#', out[4]);
}

/* TC-HX-04 */
static void test_decode_either_case(void)
{
    uint8_t out[4];
    size_t  n = 0;
    TEST_ASSERT_TRUE(wioe5_hex_decode("aBcD12eF", 8u, out, sizeof(out), &n));
    TEST_ASSERT_EQUAL_size_t(4u, n);
    const uint8_t expect[] = {0xAB, 0xCD, 0x12, 0xEF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 4);
}

/* TC-HX-05 */
static void test_decode_rejects_bad_input(void)
{
    uint8_t out[2];
    size_t  n = 0;
    TEST_ASSERT_FALSE(wioe5_hex_decode("123", 3u, out, sizeof(out), &n));    /* odd     */
    TEST_ASSERT_FALSE(wioe5_hex_decode("12GZ", 4u, out, sizeof(out), &n));   /* invalid */
    TEST_ASSERT_FALSE(wioe5_hex_decode("12 34", 5u, out, sizeof(out), &n));  /* space   */
    TEST_ASSERT_FALSE(wioe5_hex_decode("112233", 6u, out, sizeof(out), &n)); /* too big */
}

/* TC-HX-05b: zero-length input is valid. */
static void test_decode_empty(void)
{
    uint8_t out[1];
    size_t  n = 99;
    TEST_ASSERT_TRUE(wioe5_hex_decode("", 0u, out, sizeof(out), &n));
    TEST_ASSERT_EQUAL_size_t(0u, n);
}

/* TC-HX-06 */
static void test_round_trip_242_bytes(void)
{
    uint8_t  in[242], back[242];
    char     hex[2 * 242 + 1];
    size_t   n    = 0;
    uint32_t seed = 12345u;
    for (size_t i = 0; i < sizeof(in); i++) {
        seed  = seed * 1103515245u + 12345u;
        in[i] = (uint8_t)(seed >> 16);
    }
    TEST_ASSERT_TRUE(wioe5_hex_encode(in, sizeof(in), hex, sizeof(hex)));
    TEST_ASSERT_EQUAL_size_t(484u, strlen(hex));
    TEST_ASSERT_TRUE(wioe5_hex_decode(hex, strlen(hex), back, sizeof(back), &n));
    TEST_ASSERT_EQUAL_size_t(242u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, back, 242);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_uppercase_no_separators);
    RUN_TEST(test_encode_empty);
    RUN_TEST(test_encode_does_not_fit);
    RUN_TEST(test_decode_either_case);
    RUN_TEST(test_decode_rejects_bad_input);
    RUN_TEST(test_decode_empty);
    RUN_TEST(test_round_trip_242_bytes);
    return UNITY_END();
}
