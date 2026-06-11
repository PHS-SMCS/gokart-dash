#include "telemetry.h"

#include "crc16.h"

namespace kart {

namespace {

void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

}  // namespace

size_t encode_telemetry_v1(const TelemetryV1 &t, uint8_t *buf,
                           size_t buf_len) {
  if (buf_len < kTelemetryV1FrameLen) {
    return 0;
  }

  buf[0] = kFrameSync;
  buf[1] = (uint8_t)(1 + kTelemetryV1PayloadLen);  // type + payload
  buf[2] = kTypeTelemetryV1;

  uint8_t *p = &buf[3];
  p[0] = 1;  // proto_ver
  p[1] = t.drive_state;
  p[2] = t.fault_code;
  put_u16(&p[3], t.status_flags);
  p[5] = t.throttle_pct;
  p[6] = t.brake_pct;
  put_u16(&p[7], (uint16_t)t.steer_setpoint_cdeg);
  put_u16(&p[9], (uint16_t)t.steer_measured_cdeg);
  put_u32(&p[11], t.hall_count);
  put_u16(&p[15], t.hall_hz_x10);
  put_u16(&p[17], t.batt_dv);
  put_u16(&p[19], (uint16_t)t.batt_da);
  put_u16(&p[21], (uint16_t)t.esc_rpm);
  p[23] = (uint8_t)t.controller_temp_c;
  p[24] = (uint8_t)t.motor_temp_c;
  put_u32(&p[25], t.uptime_ms);
  p[29] = t.seq;

  // CRC over LEN, TYPE, PAYLOAD.
  uint16_t crc = crc16_ccitt_false(&buf[1], 2 + kTelemetryV1PayloadLen);
  put_u16(&buf[3 + kTelemetryV1PayloadLen], crc);

  return kTelemetryV1FrameLen;
}

bool decode_telemetry_v1(const uint8_t *buf, size_t len, TelemetryV1 &out) {
  if (len < kTelemetryV1FrameLen || buf[0] != kFrameSync ||
      buf[1] != 1 + kTelemetryV1PayloadLen || buf[2] != kTypeTelemetryV1) {
    return false;
  }

  uint16_t crc = crc16_ccitt_false(&buf[1], 2 + kTelemetryV1PayloadLen);
  if (crc != get_u16(&buf[3 + kTelemetryV1PayloadLen])) {
    return false;
  }

  const uint8_t *p = &buf[3];
  if (p[0] != 1) {
    return false;  // unknown proto_ver
  }
  out.drive_state = p[1];
  out.fault_code = p[2];
  out.status_flags = get_u16(&p[3]);
  out.throttle_pct = p[5];
  out.brake_pct = p[6];
  out.steer_setpoint_cdeg = (int16_t)get_u16(&p[7]);
  out.steer_measured_cdeg = (int16_t)get_u16(&p[9]);
  out.hall_count = get_u32(&p[11]);
  out.hall_hz_x10 = get_u16(&p[15]);
  out.batt_dv = get_u16(&p[17]);
  out.batt_da = (int16_t)get_u16(&p[19]);
  out.esc_rpm = (int16_t)get_u16(&p[21]);
  out.controller_temp_c = (int8_t)p[23];
  out.motor_temp_c = (int8_t)p[24];
  out.uptime_ms = get_u32(&p[25]);
  out.seq = p[29];
  return true;
}

}  // namespace kart
