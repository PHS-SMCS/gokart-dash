// Tests for CRC16, telemetry framing, and the shared CAN pack/unpack.
#include <string.h>
#include <unity.h>

#include "crc16.h"
#include "kart_can.h"
#include "telemetry.h"

void setUp() {}
void tearDown() {}

// -------------------- CRC16 --------------------

void test_crc16_ccitt_false_check_value() {
  // Standard check value: CRC-16/CCITT-FALSE("123456789") == 0x29B1.
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, kart::crc16_ccitt_false(data, sizeof(data)));
}

// -------------------- Telemetry framing --------------------

kart::TelemetryV1 sample_telemetry() {
  kart::TelemetryV1 t{};
  t.drive_state = 2;  // DRIVE
  t.fault_code = 0;
  t.status_flags = kart::kFlagWheelConnected | kart::kFlagSteerLinkOk |
                   kart::kFlagContactorClosed;
  t.throttle_pct = 37;
  t.brake_pct = 0;
  t.steer_setpoint_cdeg = -1234;
  t.steer_measured_cdeg = -1200;
  t.hall_count = 123456789;
  t.hall_hz_x10 = 4521;
  t.batt_dv = 482;
  t.batt_da = -75;
  t.esc_rpm = 3120;
  t.controller_temp_c = 41;
  t.motor_temp_c = kart::kTempUnknown;
  t.uptime_ms = 3600000;
  t.seq = 200;
  return t;
}

void test_telemetry_roundtrip() {
  kart::TelemetryV1 t = sample_telemetry();
  uint8_t buf[kart::kTelemetryV1FrameLen];
  size_t n = kart::encode_telemetry_v1(t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(kart::kTelemetryV1FrameLen, n);
  TEST_ASSERT_EQUAL_HEX8(kart::kFrameSync, buf[0]);

  kart::TelemetryV1 d{};
  TEST_ASSERT_TRUE(kart::decode_telemetry_v1(buf, n, d));
  TEST_ASSERT_EQUAL(t.drive_state, d.drive_state);
  TEST_ASSERT_EQUAL(t.status_flags, d.status_flags);
  TEST_ASSERT_EQUAL(t.throttle_pct, d.throttle_pct);
  TEST_ASSERT_EQUAL(t.steer_setpoint_cdeg, d.steer_setpoint_cdeg);
  TEST_ASSERT_EQUAL(t.steer_measured_cdeg, d.steer_measured_cdeg);
  TEST_ASSERT_EQUAL_UINT32(t.hall_count, d.hall_count);
  TEST_ASSERT_EQUAL(t.hall_hz_x10, d.hall_hz_x10);
  TEST_ASSERT_EQUAL(t.batt_da, d.batt_da);
  TEST_ASSERT_EQUAL(t.controller_temp_c, d.controller_temp_c);
  TEST_ASSERT_EQUAL(t.motor_temp_c, d.motor_temp_c);
  TEST_ASSERT_EQUAL_UINT32(t.uptime_ms, d.uptime_ms);
  TEST_ASSERT_EQUAL(t.seq, d.seq);
}

void test_telemetry_rejects_corruption() {
  kart::TelemetryV1 t = sample_telemetry();
  uint8_t buf[kart::kTelemetryV1FrameLen];
  size_t n = kart::encode_telemetry_v1(t, buf, sizeof(buf));

  kart::TelemetryV1 d{};
  uint8_t bad[kart::kTelemetryV1FrameLen];

  // Flipped payload bit -> CRC failure.
  memcpy(bad, buf, n);
  bad[10] ^= 0x01;
  TEST_ASSERT_FALSE(kart::decode_telemetry_v1(bad, n, d));

  // Corrupted CRC itself.
  memcpy(bad, buf, n);
  bad[n - 1] ^= 0xFF;
  TEST_ASSERT_FALSE(kart::decode_telemetry_v1(bad, n, d));

  // Bad sync byte.
  memcpy(bad, buf, n);
  bad[0] = 0x55;
  TEST_ASSERT_FALSE(kart::decode_telemetry_v1(bad, n, d));

  // Truncated frame.
  TEST_ASSERT_FALSE(kart::decode_telemetry_v1(buf, n - 1, d));
}

void test_telemetry_rejects_small_buffer() {
  kart::TelemetryV1 t = sample_telemetry();
  uint8_t buf[kart::kTelemetryV1FrameLen - 1];
  TEST_ASSERT_EQUAL(0, kart::encode_telemetry_v1(t, buf, sizeof(buf)));
}

// -------------------- CAN pack/unpack --------------------

void test_steer_set_roundtrip() {
  kart::SteerSet s{true, -2500, 42};
  uint8_t buf[kart::kSteerSetDlc];
  kart::pack_steer_set(s, buf);

  kart::SteerSet d{};
  TEST_ASSERT_TRUE(kart::unpack_steer_set(buf, sizeof(buf), d));
  TEST_ASSERT_TRUE(d.enable);
  TEST_ASSERT_EQUAL(-2500, d.setpoint_cdeg);
  TEST_ASSERT_EQUAL(42, d.seq);
}

void test_steer_set_rejects_reserved_flags_and_short_dlc() {
  kart::SteerSet s{true, 0, 0};
  uint8_t buf[kart::kSteerSetDlc];
  kart::pack_steer_set(s, buf);

  kart::SteerSet d{};
  TEST_ASSERT_FALSE(kart::unpack_steer_set(buf, kart::kSteerSetDlc - 1, d));

  buf[0] |= 0x80;  // reserved flag bit set
  TEST_ASSERT_FALSE(kart::unpack_steer_set(buf, sizeof(buf), d));
}

void test_steer_status_roundtrip() {
  kart::SteerStatus s{};
  s.state = kart::SteerState::kActive;
  s.fault_bits = kart::kSteerFaultStall;
  s.measured_cdeg = 1750;
  s.output_pct = -64;
  s.seq_echo = 9;
  s.pot_raw = 2913;

  uint8_t buf[kart::kSteerStatusDlc];
  kart::pack_steer_status(s, buf);

  kart::SteerStatus d{};
  TEST_ASSERT_TRUE(kart::unpack_steer_status(buf, sizeof(buf), d));
  TEST_ASSERT_EQUAL((int)kart::SteerState::kActive, (int)d.state);
  TEST_ASSERT_EQUAL(kart::kSteerFaultStall, d.fault_bits);
  TEST_ASSERT_EQUAL(1750, d.measured_cdeg);
  TEST_ASSERT_EQUAL(-64, d.output_pct);
  TEST_ASSERT_EQUAL(9, d.seq_echo);
  TEST_ASSERT_EQUAL(2913, d.pot_raw);
}

void test_steer_status_rejects_invalid_state() {
  uint8_t buf[kart::kSteerStatusDlc] = {99, 0, 0, 0, 0, 0, 0, 0};
  kart::SteerStatus d{};
  TEST_ASSERT_FALSE(kart::unpack_steer_status(buf, sizeof(buf), d));
}

void test_steer_cfg_roundtrip() {
  kart::SteerCfg c{kart::SteerCfgParam::kKp, 3.25f};
  uint8_t buf[kart::kSteerCfgDlc];
  kart::pack_steer_cfg(c, buf);

  kart::SteerCfg d{};
  TEST_ASSERT_TRUE(kart::unpack_steer_cfg(buf, sizeof(buf), d));
  TEST_ASSERT_EQUAL((int)kart::SteerCfgParam::kKp, (int)d.param);
  TEST_ASSERT_EQUAL_FLOAT(3.25f, d.value);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_ccitt_false_check_value);
  RUN_TEST(test_telemetry_roundtrip);
  RUN_TEST(test_telemetry_rejects_corruption);
  RUN_TEST(test_telemetry_rejects_small_buffer);
  RUN_TEST(test_steer_set_roundtrip);
  RUN_TEST(test_steer_set_rejects_reserved_flags_and_short_dlc);
  RUN_TEST(test_steer_status_roundtrip);
  RUN_TEST(test_steer_status_rejects_invalid_state);
  RUN_TEST(test_steer_cfg_roundtrip);
  return UNITY_END();
}
