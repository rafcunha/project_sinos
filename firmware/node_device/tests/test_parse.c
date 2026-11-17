/* L2 - response parsers (test plan section 2.3). */
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "wioe5_parse.h"

void setUp(void) {}
void tearDown(void) {}

static char         buf[600];
static wioe5_resp_t resp;

static bool parse(const char *line)
{
    snprintf(buf, sizeof(buf), "%s", line);
    memset(&resp, 0, sizeof(resp));
    return wioe5_parse_resp(buf, &resp);
}

static wioe5_ev_t ev_of(const char *body)
{
    wioe5_ev_t ev;
    memset(&ev, 0xA5, sizeof(ev));
    (void)wioe5_parse_event(body, &ev);
    return ev;
}

/* ---- Generic line -------------------------------------------------------- */

/* TC-PR-01 */
static void test_resp_simple(void)
{
    TEST_ASSERT_TRUE(parse("+AT: OK"));
    TEST_ASSERT_EQUAL_STRING("AT", resp.name);
    TEST_ASSERT_EQUAL_STRING("OK", resp.body);
    TEST_ASSERT_FALSE(resp.is_error);
    TEST_ASSERT_TRUE(wioe5_resp_name_is(&resp, "AT"));
}

/* TC-PR-02 */
static void test_resp_name_case_insensitive(void)
{
    TEST_ASSERT_TRUE(parse("+at: ok"));
    TEST_ASSERT_TRUE(wioe5_resp_name_is(&resp, "AT"));
    TEST_ASSERT_TRUE(parse("+MsgHex: Done"));
    TEST_ASSERT_TRUE(wioe5_resp_name_is(&resp, "MSGHEX"));
    TEST_ASSERT_FALSE(wioe5_resp_name_is(&resp, "MSG"));
}

/* TC-PR-03 / TC-PR-04 */
static void test_resp_error_codes(void)
{
    TEST_ASSERT_TRUE(parse("+MSG: ERROR(-12)"));
    TEST_ASSERT_TRUE(resp.is_error);
    TEST_ASSERT_EQUAL_INT16(-12, resp.error_code);
    TEST_ASSERT_TRUE(parse("+CMSGHEX: ERROR(-1)"));
    TEST_ASSERT_TRUE(resp.is_error);
    TEST_ASSERT_EQUAL_INT16(-1, resp.error_code);
    TEST_ASSERT_TRUE(wioe5_resp_name_is(&resp, "CMSGHEX"));
}

/* TC-PR-05: malformed ERROR(...) is not an error code. */
static void test_resp_malformed_error(void)
{
    TEST_ASSERT_TRUE(parse("+AT: ERROR(abc)"));
    TEST_ASSERT_FALSE(resp.is_error);
    TEST_ASSERT_TRUE(parse("+AT: ERROR("));
    TEST_ASSERT_FALSE(resp.is_error);
    TEST_ASSERT_TRUE(parse("+AT: ERROR(-1) trailing"));
    TEST_ASSERT_FALSE(resp.is_error);
}

/* TC-PR-27: body trimmed at both ends. */
static void test_resp_body_trimmed(void)
{
    TEST_ASSERT_TRUE(parse("+PORT:   8  "));
    TEST_ASSERT_EQUAL_STRING("8", resp.body);
    TEST_ASSERT_TRUE(parse("+AT:OK"));
    TEST_ASSERT_EQUAL_STRING("OK", resp.body);
    TEST_ASSERT_TRUE(parse("+AT:"));
    TEST_ASSERT_EQUAL_STRING("", resp.body);
}

/* TC-PR-28: lines that are not "+NAME:" are rejected. */
static void test_resp_rejects_non_responses(void)
{
    TEST_ASSERT_FALSE(parse("AT: OK"));
    TEST_ASSERT_FALSE(parse("+AT OK"));
    TEST_ASSERT_FALSE(parse("+: OK"));
    TEST_ASSERT_FALSE(parse("+A-T: OK"));
    TEST_ASSERT_FALSE(parse("+ABCDEFGHIJKLMNOPQ: x")); /* 17-char name */
    TEST_ASSERT_TRUE(parse("+ABCDEFGHIJKLMNOP: x"));   /* 16-char name */
    /* AT+DELAY answers without ':' (PDF 4.27); the driver never sends it. */
    TEST_ASSERT_FALSE(parse("+DELAY RX1, 1000"));
}

/* ---- Simple bodies ------------------------------------------------------- */

/* TC-PR-06 */
static void test_ver(void)
{
    char v[16];
    TEST_ASSERT_TRUE(wioe5_parse_ver("4.0.11", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("4.0.11", v);
    TEST_ASSERT_FALSE(wioe5_parse_ver("", v, sizeof(v)));
    TEST_ASSERT_FALSE(wioe5_parse_ver("1234567890.123456", v, sizeof(v))); /* does not fit */
}

/* TC-PR-07 */
static void test_id_deveui(void)
{
    uint8_t       id[8];
    const uint8_t expect[] = {0x2C, 0xF7, 0xF1, 0x20, 0x42, 0x00, 0x12, 0x34};
    TEST_ASSERT_TRUE(wioe5_parse_id("DevEui, 2C:F7:F1:20:42:00:12:34", "DevEui", id, 8));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, id, 8);
    TEST_ASSERT_TRUE(wioe5_parse_id("DEVEUI, 2c:f7:f1:20:42:00:12:34", "DevEui", id, 8));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, id, 8);
}

/* TC-PR-08 */
static void test_id_rejects_bad(void)
{
    uint8_t id[8];
    TEST_ASSERT_FALSE(wioe5_parse_id("DevEui, 2C:F7:F1", "DevEui", id, 8));                   /* short */
    TEST_ASSERT_FALSE(wioe5_parse_id("DevEui, 2C:F7:F1:20:42:00:12:34:56", "DevEui", id, 8)); /* long  */
    TEST_ASSERT_FALSE(wioe5_parse_id("DevEui, 2C:F7:F1:20:42:00:12:3G", "DevEui", id, 8));    /* digit */
    TEST_ASSERT_FALSE(wioe5_parse_id("AppEui, 2C:F7:F1:20:42:00:12:34", "DevEui", id, 8));    /* field */
}

/* TC-PR-07b: DevAddr, 4 bytes. */
static void test_id_devaddr(void)
{
    uint8_t       a[4];
    const uint8_t expect[] = {0x01, 0x23, 0x45, 0x67};
    TEST_ASSERT_TRUE(wioe5_parse_id("DevAddr, 01:23:45:67", "DevAddr", a, 4));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, a, 4);
}

/* TC-PR-09 */
static void test_lw_len(void)
{
    uint16_t n = 999;
    TEST_ASSERT_TRUE(wioe5_parse_lw_len("LEN, 50", &n));
    TEST_ASSERT_EQUAL_UINT16(50, n);
    TEST_ASSERT_TRUE(wioe5_parse_lw_len("LEN, 0", &n));
    TEST_ASSERT_EQUAL_UINT16(0, n);
    TEST_ASSERT_FALSE(wioe5_parse_lw_len("LEN, ", &n));
    TEST_ASSERT_FALSE(wioe5_parse_lw_len("DC, ON, 0", &n));
}

static void test_uint_body(void)
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(wioe5_parse_uint_body("8", &v));
    TEST_ASSERT_EQUAL_UINT32(8u, v);
    TEST_ASSERT_TRUE(wioe5_parse_uint_body(" 255 ", &v));
    TEST_ASSERT_EQUAL_UINT32(255u, v);
    TEST_ASSERT_FALSE(wioe5_parse_uint_body("", &v));
    TEST_ASSERT_FALSE(wioe5_parse_uint_body("-1", &v));
    TEST_ASSERT_FALSE(wioe5_parse_uint_body("8x", &v));
}

/* ---- JOIN / MSG events --------------------------------------------------- */

/* TC-PR-10 */
static void test_ev_netid(void)
{
    wioe5_ev_t    ev       = ev_of("NetID 000013 DevAddr 26:0B:12:34");
    const uint8_t expect[] = {0x26, 0x0B, 0x12, 0x34};
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_NETID, ev.kind);
    TEST_ASSERT_EQUAL_HEX32(0x000013u, ev.net_id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, ev.dev_addr, 4);
}

static void test_ev_simple_keywords(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_START, ev_of("Start").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_START, ev_of("Starting").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_DONE, ev_of("Done").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_DONE, ev_of("done").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_NORMAL, ev_of("NORMAL").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_FPENDING, ev_of("FPENDING").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_MULTICAST, ev_of("MULTICAST").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_TX, ev_of("TX \"\"").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_UNKNOWN, ev_of("something else").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_UNKNOWN, ev_of("").kind);
}

/* TC-PR-11 / TC-PR-12 */
static void test_ev_join_results(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_JOIN_FAILED, ev_of("Join failed").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_JOINED_ALREADY, ev_of("Joined already").kind);
}

/* TC-PR-13 */
static void test_ev_no_band(void)
{
    wioe5_ev_t ev = ev_of("No band in 13469ms");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_NO_BAND, ev.kind);
    TEST_ASSERT_EQUAL_UINT32(13469u, ev.value);
    ev = ev_of("No band in 5000 ms");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_NO_BAND, ev.kind);
    TEST_ASSERT_EQUAL_UINT32(5000u, ev.value);
}

/* TC-PR-14 */
static void test_ev_no_free_channel_and_dr(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_NO_FREE_CH, ev_of("No free channel -70").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_NO_FREE_CH, ev_of("No free channel").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_DR, ev_of("DR error").kind);
}

/* TC-PR-15 */
static void test_ev_length_error(void)
{
    wioe5_ev_t ev = ev_of("Length error 0");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_LENGTH, ev.kind);
    TEST_ASSERT_EQUAL_UINT32(0u, ev.value);
    ev = ev_of("Length error 12");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_LENGTH, ev.kind);
    TEST_ASSERT_EQUAL_UINT32(12u, ev.value);
}

/* TC-PR-16 / TC-PR-17 */
static void test_ev_busy_and_not_joined(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_BUSY, ev_of("LoRaWAN modem is busy").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ERR_NOT_JOINED, ev_of("Please join network first").kind);
}

/* TC-PR-18 */
static void test_ev_downlink(void)
{
    wioe5_ev_t ev = ev_of("PORT: 8; RX: \"12345678\"");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_DOWNLINK, ev.kind);
    TEST_ASSERT_EQUAL_UINT8(8u, ev.port);
    TEST_ASSERT_EQUAL_size_t(8u, ev.hex_len);
    TEST_ASSERT_EQUAL_STRING_LEN("12345678", ev.hex, 8);
}

/* TC-PR-19 */
static void test_ev_downlink_empty(void)
{
    wioe5_ev_t ev = ev_of("PORT: 200; RX: \"\"");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_DOWNLINK, ev.kind);
    TEST_ASSERT_EQUAL_UINT8(200u, ev.port);
    TEST_ASSERT_EQUAL_size_t(0u, ev.hex_len);
}

/* TC-PR-20: the parser passes the hex through; validation is in hex_decode.
 * A missing closing quote or port out of range is malformed. */
static void test_ev_downlink_malformed(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_DOWNLINK, ev_of("PORT: 8; RX: \"123\"").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_UNKNOWN, ev_of("PORT: 8; RX: \"1234").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_UNKNOWN, ev_of("PORT: 256; RX: \"12\"").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_UNKNOWN, ev_of("PORT: ; RX: \"12\"").kind);
}

/* TC-PR-21 */
static void test_ev_rxinfo(void)
{
    wioe5_ev_t ev = ev_of("RXWIN1, RSSI -93, SNR 6.25");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_RXINFO, ev.kind);
    TEST_ASSERT_EQUAL_UINT8(1u, ev.rx_window);
    TEST_ASSERT_EQUAL_INT16(-93, ev.rssi_dbm);
    TEST_ASSERT_EQUAL_INT16(625, ev.snr_cdb);
}

/* TC-PR-22 */
static void test_ev_rxinfo_variants(void)
{
    wioe5_ev_t ev = ev_of("RXWIN2, RSSI -106, SNR -7.5");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_RXINFO, ev.kind);
    TEST_ASSERT_EQUAL_UINT8(2u, ev.rx_window);
    TEST_ASSERT_EQUAL_INT16(-106, ev.rssi_dbm);
    TEST_ASSERT_EQUAL_INT16(-750, ev.snr_cdb);
    ev = ev_of("RXWIN0, RSSI -50, SNR 4");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_RXINFO, ev.kind);
    TEST_ASSERT_EQUAL_UINT8(0u, ev.rx_window);
    TEST_ASSERT_EQUAL_INT16(400, ev.snr_cdb);
}

/* TC-PR-23 */
static void test_ev_link(void)
{
    wioe5_ev_t ev = ev_of("Link 20, 1");
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_LINK, ev.kind);
    TEST_ASSERT_EQUAL_UINT8(20u, ev.link_margin);
    TEST_ASSERT_EQUAL_UINT8(1u, ev.link_gw);
}

/* TC-PR-24 */
static void test_ev_ack_and_wait_ack(void)
{
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_ACK, ev_of("ACK Received").kind);
    TEST_ASSERT_EQUAL_INT(WIOE5_EV_WAIT_ACK, ev_of("Wait ACK").kind);
}

/* SNR helper */
static void test_centi(void)
{
    int16_t v = 0;
    TEST_ASSERT_TRUE(wioe5_parse_centi("6.25", 4, &v));
    TEST_ASSERT_EQUAL_INT16(625, v);
    TEST_ASSERT_TRUE(wioe5_parse_centi("-7.5", 4, &v));
    TEST_ASSERT_EQUAL_INT16(-750, v);
    TEST_ASSERT_TRUE(wioe5_parse_centi("4", 1, &v));
    TEST_ASSERT_EQUAL_INT16(400, v);
    TEST_ASSERT_TRUE(wioe5_parse_centi("-0.125", 6, &v));
    TEST_ASSERT_EQUAL_INT16(-12, v);
    TEST_ASSERT_FALSE(wioe5_parse_centi("", 0, &v));
    TEST_ASSERT_FALSE(wioe5_parse_centi("abc", 3, &v));
    TEST_ASSERT_FALSE(wioe5_parse_centi("1.2.3", 5, &v));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_resp_simple);
    RUN_TEST(test_resp_name_case_insensitive);
    RUN_TEST(test_resp_error_codes);
    RUN_TEST(test_resp_malformed_error);
    RUN_TEST(test_resp_body_trimmed);
    RUN_TEST(test_resp_rejects_non_responses);
    RUN_TEST(test_ver);
    RUN_TEST(test_id_deveui);
    RUN_TEST(test_id_rejects_bad);
    RUN_TEST(test_id_devaddr);
    RUN_TEST(test_lw_len);
    RUN_TEST(test_uint_body);
    RUN_TEST(test_ev_netid);
    RUN_TEST(test_ev_simple_keywords);
    RUN_TEST(test_ev_join_results);
    RUN_TEST(test_ev_no_band);
    RUN_TEST(test_ev_no_free_channel_and_dr);
    RUN_TEST(test_ev_length_error);
    RUN_TEST(test_ev_busy_and_not_joined);
    RUN_TEST(test_ev_downlink);
    RUN_TEST(test_ev_downlink_empty);
    RUN_TEST(test_ev_downlink_malformed);
    RUN_TEST(test_ev_rxinfo);
    RUN_TEST(test_ev_rxinfo_variants);
    RUN_TEST(test_ev_link);
    RUN_TEST(test_ev_ack_and_wait_ack);
    RUN_TEST(test_centi);
    return UNITY_END();
}
