// CTRE Talon SRX CAN frame builders (non-FRC control scheme).
//
// Frame layouts ported from willGuimont/CanControl (MIT), which derives them
// from CTRE's published non-RoboRIO Phoenix example:
//   - PercentOutput control frame: 29-bit ID 0x02040080 | device_number,
//     DLC 8, bytes 0..2 = signed 24-bit demand (percent * 1023, MSB first).
//   - Global enable frame: 29-bit ID 0x000401BF, DLC 8, byte 0 = 1/0.
//
// The Talon disables its output if enable frames stop arriving (~100 ms),
// which is the hardware-level failsafe this design leans on.
#pragma once

#include <stdint.h>

namespace steervo {

struct CanFrame {
  uint32_t id;        // 29-bit arbitration ID (always extended for CTRE)
  uint8_t dlc;
  uint8_t data[8];
};

constexpr uint32_t kTalonSrxBaseId = 0x02040000u;
constexpr uint32_t kTalonControlArb = 0x00040080u;
constexpr uint32_t kCtreGlobalEnableId = 0x000401BFu;
constexpr int16_t kTalonOutputMax = 1023;

inline CanFrame talon_percent_output(uint8_t device_number, float percent) {
  if (percent > 1.0f) percent = 1.0f;
  if (percent < -1.0f) percent = -1.0f;
  int32_t demand = (int32_t)(percent * (float)kTalonOutputMax);

  CanFrame f{};
  f.id = kTalonSrxBaseId | kTalonControlArb | (device_number & 0x3Fu);
  f.dlc = 8;
  f.data[0] = (uint8_t)((demand >> 16) & 0xFF);
  f.data[1] = (uint8_t)((demand >> 8) & 0xFF);
  f.data[2] = (uint8_t)(demand & 0xFF);
  return f;
}

inline CanFrame ctre_global_enable(bool enable) {
  CanFrame f{};
  f.id = kCtreGlobalEnableId;
  f.dlc = 8;
  f.data[0] = enable ? 1 : 0;
  return f;
}

}  // namespace steervo
