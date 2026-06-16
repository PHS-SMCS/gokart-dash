// Binary telemetry framing (Teensy -> Pi).
// Source of truth: docs/protocols/uart-protocol.md — change the doc first.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kart {

constexpr uint8_t kFrameSync = 0xF7;
constexpr uint8_t kTypeTelemetryV1 = 0x01;
constexpr uint8_t kTypeEvent = 0x02;

constexpr size_t kTelemetryV1PayloadLen = 30;
// sync + len + type + payload + crc16
constexpr size_t kTelemetryV1FrameLen = 3 + kTelemetryV1PayloadLen + 2;

// status_flags bits
constexpr uint16_t kFlagWheelConnected = 1 << 0;
constexpr uint16_t kFlagSteerLinkOk = 1 << 1;
constexpr uint16_t kFlagSteerCalibrated = 1 << 2;
constexpr uint16_t kFlagEscLinkOk = 1 << 3;
constexpr uint16_t kFlagContactorClosed = 1 << 4;
constexpr uint16_t kFlagReverse = 1 << 5;
constexpr uint16_t kFlagBrakeActive = 1 << 6;
constexpr uint16_t kFlagRcLinkUp = 1 << 7;
constexpr uint16_t kFlagBenchMode = 1 << 8;

constexpr int8_t kTempUnknown = -128;

struct TelemetryV1 {
  uint8_t drive_state;
  uint8_t fault_code;
  uint16_t status_flags;
  uint8_t throttle_pct;
  uint8_t brake_pct;
  int16_t steer_setpoint_cdeg;
  int16_t steer_measured_cdeg;
  uint32_t hall_count;
  uint16_t hall_hz_x10;
  uint16_t batt_dv;
  int16_t batt_da;
  int16_t esc_rpm;
  int8_t controller_temp_c;
  int8_t motor_temp_c;
  uint32_t uptime_ms;
  uint8_t seq;
};

// Encodes a complete frame (sync..crc) into buf. Returns the frame length,
// or 0 if buf_len is too small.
size_t encode_telemetry_v1(const TelemetryV1 &t, uint8_t *buf, size_t buf_len);

// Decodes a complete frame. Returns false on bad sync/len/type/CRC.
// (Reference implementation for tests; the Pi-side decoder lives in pi/.)
bool decode_telemetry_v1(const uint8_t *buf, size_t len, TelemetryV1 &out);

}  // namespace kart
