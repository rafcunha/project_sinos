/* L1 - line assembler (test plan section 2.2). */
#include <string.h>

#include "unity.h"
#include "wioe5_line.h"

#define MAXLEN 32u

static wioe5_lineasm_t la;
static char            storage[MAXLEN + 1u];

/* Collected output of a feed run. */
static char lines[8][MAXLEN + 1u];
static int  n_lines;
static int  n_overflows;

void setUp(void)
{
    memset(storage, 0x7E, sizeof(storage));
    wioe5_lineasm_init(&la, storage, MAXLEN);
    memset(lines, 0, sizeof(lines));
    n_lines     = 0;
    n_overflows = 0;
}

void tearDown(void) {}

static void feed(const char *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char               *line = NULL;
        wioe5_line_result_t r    = wioe5_lineasm_feed(&la, (uint8_t)bytes[i], &line);
        if (r == WIOE5_LINE_READY) {
            TEST_ASSERT_NOT_NULL(line);
            if (n_lines < 8) {
                strncpy(lines[n_lines], line, MAXLEN);
            }
            n_lines++;
        } else if (r == WIOE5_LINE_OVERFLOW) {
            n_overflows++;
        }
    }
}

#define FEED(lit) feed((lit), sizeof(lit) - 1u)

/* TC-LN-01 */
static void test_crlf_line(void)
{
    FEED("+AT: OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

/* TC-LN-02 */
static void test_lf_only_line(void)
{
    FEED("+AT: OK\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

/* TC-LN-03 */
static void test_line_fed_in_chunks(void)
{
    FEED("+VER");
    TEST_ASSERT_EQUAL_INT(0, n_lines);
    FEED(": 4.0");
    FEED(".11\r");
    TEST_ASSERT_EQUAL_INT(0, n_lines);
    FEED("\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+VER: 4.0.11", lines[0]);
}

/* TC-LN-04 */
static void test_two_lines_in_one_block(void)
{
    FEED("+MSG: Start\r\n+MSG: Done\r\n");
    TEST_ASSERT_EQUAL_INT(2, n_lines);
    TEST_ASSERT_EQUAL_STRING("+MSG: Start", lines[0]);
    TEST_ASSERT_EQUAL_STRING("+MSG: Done", lines[1]);
}

/* TC-LN-05 */
static void test_empty_lines_ignored_silently(void)
{
    FEED("\r\n\r\n\n");
    TEST_ASSERT_EQUAL_INT(0, n_lines);
    TEST_ASSERT_EQUAL_UINT32(0u, la.unknown_lines);
}

/* TC-LN-06 */
static void test_wakeup_0x55_before_plus_dropped(void)
{
    FEED("\x55+LOWPOWER: WAKEUP\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+LOWPOWER: WAKEUP", lines[0]);
}

/* TC-LN-07 */
static void test_ff_preamble_dropped(void)
{
    FEED("\xFF\xFF\xFF\xFF+AT: OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

/* TC-LN-08 */
static void test_line_without_plus_is_unknown(void)
{
    FEED("garbage\r\n");
    TEST_ASSERT_EQUAL_INT(0, n_lines);
    TEST_ASSERT_EQUAL_UINT32(1u, la.unknown_lines);
    FEED("+AT: OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
}

/* TC-LN-09: exactly MAXLEN characters is accepted. */
static void test_line_of_max_length_accepted(void)
{
    char buf[MAXLEN + 3u];
    memset(buf, 'A', sizeof(buf));
    buf[0]          = '+';
    buf[MAXLEN]     = '\r';
    buf[MAXLEN + 1] = '\n';
    feed(buf, MAXLEN + 2u);
    TEST_ASSERT_EQUAL_INT(0, n_overflows);
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_size_t(MAXLEN, strlen(lines[0]));
}

/* TC-LN-10: MAXLEN + 1 overflows once, then the next line is intact. */
static void test_overlong_line_dropped_next_line_intact(void)
{
    char buf[MAXLEN + 10u];
    memset(buf, 'B', sizeof(buf));
    buf[0] = '+';
    feed(buf, MAXLEN + 5u); /* well past the limit */
    FEED("\r\n+AT: OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_overflows);
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

/* TC-LN-11: NUL bytes are dropped wherever they appear. */
static void test_nul_bytes_dropped(void)
{
    static const char in[] = {'+', 'A', '\0', 'T', ':', ' ', 'O', 'K', '\r', '\n'};
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

/* TC-LN-12: '\r' in the middle of a line is dropped. */
static void test_cr_inside_line_dropped(void)
{
    FEED("+A\rT: OK\r\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

/* TC-LN-13: reset forgets a partial line. */
static void test_reset_drops_partial_line(void)
{
    FEED("+VER: 4.0");
    wioe5_lineasm_reset(&la);
    FEED(".11\r\n+AT: OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, n_lines);
    TEST_ASSERT_EQUAL_STRING("+AT: OK", lines[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crlf_line);
    RUN_TEST(test_lf_only_line);
    RUN_TEST(test_line_fed_in_chunks);
    RUN_TEST(test_two_lines_in_one_block);
    RUN_TEST(test_empty_lines_ignored_silently);
    RUN_TEST(test_wakeup_0x55_before_plus_dropped);
    RUN_TEST(test_ff_preamble_dropped);
    RUN_TEST(test_line_without_plus_is_unknown);
    RUN_TEST(test_line_of_max_length_accepted);
    RUN_TEST(test_overlong_line_dropped_next_line_intact);
    RUN_TEST(test_nul_bytes_dropped);
    RUN_TEST(test_cr_inside_line_dropped);
    RUN_TEST(test_reset_drops_partial_line);
    return UNITY_END();
}
