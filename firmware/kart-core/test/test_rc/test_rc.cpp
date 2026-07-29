// Tests for the CRSF receiver decoder (rc_link).
#include <string.h>
#include <unity.h>

#include "rc_link.h"

void setUp() {}
void tearDown() {}

// Pack 16 channels (11 bits each, little-endian) into 22 payload bytes — the
// independent inverse of RcLink::unpack_channels.
static void packChannels(const uint16_t ch[16], uint8_t out[22]) {
  memset(out, 0, 22);
  int bit = 0;
  for (int c = 0; c < 16; c++) {
    uint16_t v = ch[c] & 0x07FF;
    for (int i = 0; i < 11; i++) {
      if (v & (1u << i)) out[bit >> 3] |= (uint8_t)(1u << (bit & 7));
      bit++;
    }
  }
}

// Build a full RC_CHANNELS_PACKED frame into out[26]. Returns the length (26).
static uint8_t buildRcFrame(const uint16_t ch[16], uint8_t out[26]) {
  out[0] = 0xC8;  // sync
  out[1] = 24;    // len = type(1) + payload(22) + crc(1)
  out[2] = 0x16;  // RC_CHANNELS_PACKED
  packChannels(ch, &out[3]);
  out[25] = kart::crsf_crc8(&out[2], 23);  // crc over type+payload
  return 26;
}

static void feedAll(kart::RcLink &rc, const uint8_t *buf, uint8_t n, uint32_t now) {
  for (uint8_t i = 0; i < n; i++) rc.feed(buf[i], now);
}

// -------------------- Framing / unpack --------------------

void test_valid_frame_decodes_channels() {
  uint16_t ch[16];
  for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(200 + i * 100);  // distinct
  uint8_t frame[26];
  uint8_t n = buildRcFrame(ch, frame);

  kart::RcLink rc;
  feedAll(rc, frame, n, 1000);

  TEST_ASSERT_TRUE(rc.link_up(1000));
  TEST_ASSERT_EQUAL_UINT32(1, rc.frame_count());
  TEST_ASSERT_EQUAL_UINT32(0, rc.bad_crc_count());
  for (int i = 0; i < 16; i++) {
    TEST_ASSERT_EQUAL_UINT16(ch[i], rc.channel(i));
  }
}

void test_all_zero_and_all_max_payload() {
  // All payload bytes 0xFF -> every 11-bit channel is 0x7FF (2047).
  uint16_t hi[16];
  for (int i = 0; i < 16; i++) hi[i] = 0x07FF;
  uint8_t frame[26];
  buildRcFrame(hi, frame);
  kart::RcLink rc;
  feedAll(rc, frame, 26, 5);
  for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_UINT16(0x07FF, rc.channel(i));

  // All zero -> every channel 0.
  uint16_t lo[16] = {0};
  buildRcFrame(lo, frame);
  feedAll(rc, frame, 26, 6);
  for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_UINT16(0, rc.channel(i));
}

void test_bad_crc_is_rejected() {
  uint16_t ch[16];
  for (int i = 0; i < 16; i++) ch[i] = 992;
  uint8_t frame[26];
  buildRcFrame(ch, frame);
  frame[10] ^= 0xFF;  // corrupt a payload byte, leave the (now wrong) CRC

  kart::RcLink rc;
  feedAll(rc, frame, 26, 1000);

  TEST_ASSERT_FALSE(rc.link_up(1000));       // no valid frame -> link never up
  TEST_ASSERT_EQUAL_UINT32(0, rc.frame_count());
  TEST_ASSERT_EQUAL_UINT32(1, rc.bad_crc_count());
}

void test_resync_after_garbage() {
  uint16_t ch[16];
  for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(300 + i);
  uint8_t frame[26];
  buildRcFrame(ch, frame);

  kart::RcLink rc;
  // Leading noise — including a stray sync byte that starts a bogus short frame
  // and can swallow the next sync — must not desync the stream permanently.
  // CRSF frames arrive continuously, so the parser realigns within a frame or
  // two: feed garbage then two back-to-back clean frames and it must decode.
  uint8_t garbage[] = {0x00, 0x11, 0xC8, 0x02, 0x99};
  feedAll(rc, garbage, sizeof(garbage), 1000);
  feedAll(rc, frame, 26, 1001);
  feedAll(rc, frame, 26, 1002);

  TEST_ASSERT_TRUE(rc.link_up(1002));
  for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_UINT16(ch[i], rc.channel(i));
}

// -------------------- Link timeout (dead-man) --------------------

void test_link_times_out() {
  uint16_t ch[16] = {0};
  uint8_t frame[26];
  buildRcFrame(ch, frame);

  kart::RcLink rc;  // default 500 ms timeout
  feedAll(rc, frame, 26, 1000);

  TEST_ASSERT_TRUE(rc.link_up(1000));
  TEST_ASSERT_TRUE(rc.link_up(1499));   // still fresh
  TEST_ASSERT_FALSE(rc.link_up(1500));  // exactly at the timeout -> down
  TEST_ASSERT_FALSE(rc.link_up(2000));
}

// -------------------- Helpers --------------------

void test_channel_pct_maps_and_clamps() {
  uint16_t ch[16] = {0};
  ch[0] = 172;               // low endpoint
  ch[1] = 1811;              // high endpoint
  ch[2] = 992;               // ~center
  ch[3] = 100;               // below min -> clamp 0
  uint8_t frame[26];
  buildRcFrame(ch, frame);
  kart::RcLink rc;
  feedAll(rc, frame, 26, 1);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, rc.channel_pct(0));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, rc.channel_pct(1));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 50.0f, rc.channel_pct(2));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rc.channel_pct(3));
}

void test_switch_high_threshold() {
  uint16_t ch[16] = {0};
  ch[0] = 172;   // low position
  ch[1] = 1811;  // high position
  uint8_t frame[26];
  buildRcFrame(ch, frame);
  kart::RcLink rc;
  feedAll(rc, frame, 26, 1);

  TEST_ASSERT_FALSE(rc.switch_high(0));
  TEST_ASSERT_TRUE(rc.switch_high(1));
}

void test_channel_axis_centers_and_signs() {
  uint16_t ch[16] = {0};
  ch[0] = 992;   // center -> ~0
  ch[1] = 1811;  // full high -> +scale
  ch[2] = 172;   // full low -> -scale
  uint8_t frame[26];
  buildRcFrame(ch, frame);
  kart::RcLink rc;
  feedAll(rc, frame, 26, 1);

  TEST_ASSERT_INT32_WITHIN(400, 0, rc.channel_axis(0));
  TEST_ASSERT_INT32_WITHIN(200, 32767, rc.channel_axis(1));
  TEST_ASSERT_INT32_WITHIN(200, -32767, rc.channel_axis(2));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_frame_decodes_channels);
  RUN_TEST(test_all_zero_and_all_max_payload);
  RUN_TEST(test_bad_crc_is_rejected);
  RUN_TEST(test_resync_after_garbage);
  RUN_TEST(test_link_times_out);
  RUN_TEST(test_channel_pct_maps_and_clamps);
  RUN_TEST(test_switch_high_threshold);
  RUN_TEST(test_channel_axis_centers_and_signs);
  return UNITY_END();
}
