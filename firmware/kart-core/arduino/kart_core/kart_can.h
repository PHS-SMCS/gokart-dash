// Teensy <-> Steervo CAN message definitions.
// Source of truth: docs/protocols/can-ids.md — change the doc first.
//
// All kart traffic uses 11-bit standard IDs. The Talon SRX is PWM-driven and
// NOT on this bus, so the only traffic is the Teensy<->Steervo frames below.
// All fields little-endian.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kart {

// -------------------- Bus parameters --------------------
// The Talon SRX is PWM-driven and off this bus, so the bitrate is ours to pick.
// The only traffic is two 50 Hz frames (~13 kbit/s), so we run a deliberately
// slow bitrate for noise margin on a bus whose differential termination is
// non-ideal: a lower bitrate means a longer bit time, giving reflections from
// imperfect termination time to settle before each sample point.
//
// Defined as a macro (not just the constexpr) because the ESP32 TWAI driver
// selects its bit timing with a compile-time TWAI_TIMING_CONFIG_* switch — the
// macro keeps that selection and the Teensy FlexCAN setBaudRate() in lockstep.
// BOTH nodes must agree. Supported values (have a TWAI mapping in steervo):
// 1000000, 500000, 250000, 125000.
//
// NB: an earlier ~4-6% STEER_STATUS loss (periodic ~100-300 ms gaps that flapped
// STEER_LINK_OK) was NOT bitrate/bit-timing — dropping to 125000 changed nothing,
// and CAN error counters on both nodes stayed clean through the gaps. Root cause
// was Teensy-side: polled FlexCAN_T4 read() services the RX FIFO only ~50% of
// calls, overflowing the 6-deep hardware FIFO and dropping (still-ACKed) frames.
// Fixed by making RX interrupt-driven (enableFIFOInterrupt + onReceive; see
// kart-core main.cpp). Bitrate stays at the intended 250000.
#define KART_CAN_BITRATE 250000
constexpr uint32_t kCanBitrate = KART_CAN_BITRATE;  // Hz; both nodes must match

// -------------------- IDs (11-bit standard) --------------------
constexpr uint32_t kIdSteerSet = 0x100;     // Teensy -> Steervo, 50 Hz
constexpr uint32_t kIdSteerStatus = 0x101;  // Steervo -> Teensy, 50 Hz
constexpr uint32_t kIdSteerCal = 0x102;     // Teensy -> Steervo, on demand
constexpr uint32_t kIdSteerCfg = 0x103;     // Teensy -> Steervo, bench only

// -------------------- Steering range --------------------
// Logical full-lock steering angle. The wheel's full deflection maps to
// ±kSteerRangeCdeg on the Teensy, and the Steervo calibration assigns these
// same angles to the captured left/right pot stops. Both sides MUST agree so a
// wheel setpoint means the same physical position the pot was calibrated to.
constexpr int16_t kSteerRangeCdeg = 3000;  // ±30.00°

// -------------------- Rates / timeouts (safety-critical) --------------------
constexpr uint32_t kSteerSetPeriodMs = 20;       // 50 Hz
constexpr uint32_t kSteerStatusPeriodMs = 20;    // 50 Hz
constexpr uint32_t kSteerSetTimeoutMs = 150;     // Steervo: motor off
constexpr uint32_t kSteerStatusTimeoutMs = 150;  // Teensy: controlled stop

// -------------------- STEER_SET (0x100), DLC 4 --------------------
constexpr uint8_t kSteerSetDlc = 4;
constexpr uint8_t kSteerSetFlagEnable = 0x01;

struct SteerSet {
  bool enable;
  int16_t setpoint_cdeg;  // +100 = 1.00 degree right of center
  uint8_t seq;
};

inline void pack_steer_set(const SteerSet &m, uint8_t buf[kSteerSetDlc]) {
  buf[0] = m.enable ? kSteerSetFlagEnable : 0;
  buf[1] = (uint8_t)(m.setpoint_cdeg & 0xFF);
  buf[2] = (uint8_t)((m.setpoint_cdeg >> 8) & 0xFF);
  buf[3] = m.seq;
}

inline bool unpack_steer_set(const uint8_t *buf, uint8_t dlc, SteerSet &out) {
  if (dlc < kSteerSetDlc) {
    return false;
  }
  if (buf[0] & ~kSteerSetFlagEnable) {
    return false;  // reserved flag bits must be zero
  }
  out.enable = (buf[0] & kSteerSetFlagEnable) != 0;
  out.setpoint_cdeg = (int16_t)((uint16_t)buf[1] | ((uint16_t)buf[2] << 8));
  out.seq = buf[3];
  return true;
}

// -------------------- STEER_STATUS (0x101), DLC 8 --------------------
constexpr uint8_t kSteerStatusDlc = 8;

enum class SteerState : uint8_t {
  kInit = 0,
  kReady = 1,
  kActive = 2,
  kFault = 3,
  kCalibrating = 4,
};

// fault_bits
constexpr uint8_t kSteerFaultPotRange = 1 << 0;
constexpr uint8_t kSteerFaultPotFrozen = 1 << 1;
constexpr uint8_t kSteerFaultStall = 1 << 2;
constexpr uint8_t kSteerFaultSetpointStale = 1 << 3;
constexpr uint8_t kSteerFaultTalonLost = 1 << 4;
constexpr uint8_t kSteerFaultNotCalibrated = 1 << 5;
constexpr uint8_t kSteerFaultOverTravel = 1 << 6;  // pot past a calibrated stop

struct SteerStatus {
  SteerState state;
  uint8_t fault_bits;
  int16_t measured_cdeg;
  int8_t output_pct;  // -100..100
  uint8_t seq_echo;
  uint16_t pot_raw;
};

inline void pack_steer_status(const SteerStatus &m,
                              uint8_t buf[kSteerStatusDlc]) {
  buf[0] = (uint8_t)m.state;
  buf[1] = m.fault_bits;
  buf[2] = (uint8_t)(m.measured_cdeg & 0xFF);
  buf[3] = (uint8_t)((m.measured_cdeg >> 8) & 0xFF);
  buf[4] = (uint8_t)m.output_pct;
  buf[5] = m.seq_echo;
  buf[6] = (uint8_t)(m.pot_raw & 0xFF);
  buf[7] = (uint8_t)((m.pot_raw >> 8) & 0xFF);
}

inline bool unpack_steer_status(const uint8_t *buf, uint8_t dlc,
                                SteerStatus &out) {
  if (dlc < kSteerStatusDlc) {
    return false;
  }
  if (buf[0] > (uint8_t)SteerState::kCalibrating) {
    return false;
  }
  out.state = (SteerState)buf[0];
  out.fault_bits = buf[1];
  out.measured_cdeg = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
  out.output_pct = (int8_t)buf[4];
  out.seq_echo = buf[5];
  out.pot_raw = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
  return true;
}

// -------------------- STEER_CAL (0x102), DLC 2 --------------------
constexpr uint8_t kSteerCalDlc = 2;

enum class SteerCalCmd : uint8_t {
  kEnter = 0,
  kMarkCenter = 1,
  kMarkLeft = 2,
  kMarkRight = 3,
  kSaveExit = 4,
  kAbort = 5,
};

inline void pack_steer_cal(SteerCalCmd cmd, uint8_t buf[kSteerCalDlc]) {
  buf[0] = (uint8_t)cmd;
  buf[1] = 0;
}

inline bool unpack_steer_cal(const uint8_t *buf, uint8_t dlc,
                             SteerCalCmd &out) {
  if (dlc < kSteerCalDlc || buf[0] > (uint8_t)SteerCalCmd::kAbort) {
    return false;
  }
  out = (SteerCalCmd)buf[0];
  return true;
}

// -------------------- STEER_CFG (0x103), DLC 8 --------------------
constexpr uint8_t kSteerCfgDlc = 8;

enum class SteerCfgParam : uint8_t {
  kKp = 1,
  kKi = 2,
  kKd = 3,
  kOutputLimitPct = 4,
  kSoftLimitMarginCdeg = 5,
};

struct SteerCfg {
  SteerCfgParam param;
  float value;
};

inline void pack_steer_cfg(const SteerCfg &m, uint8_t buf[kSteerCfgDlc]) {
  buf[0] = (uint8_t)m.param;
  uint32_t bits;
  static_assert(sizeof(bits) == sizeof(m.value), "float must be 32-bit");
  __builtin_memcpy(&bits, &m.value, sizeof(bits));
  buf[1] = (uint8_t)(bits & 0xFF);
  buf[2] = (uint8_t)((bits >> 8) & 0xFF);
  buf[3] = (uint8_t)((bits >> 16) & 0xFF);
  buf[4] = (uint8_t)((bits >> 24) & 0xFF);
  buf[5] = buf[6] = buf[7] = 0;
}

inline bool unpack_steer_cfg(const uint8_t *buf, uint8_t dlc, SteerCfg &out) {
  if (dlc < kSteerCfgDlc || buf[0] < (uint8_t)SteerCfgParam::kKp ||
      buf[0] > (uint8_t)SteerCfgParam::kSoftLimitMarginCdeg) {
    return false;
  }
  out.param = (SteerCfgParam)buf[0];
  uint32_t bits = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) |
                  ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);
  __builtin_memcpy(&out.value, &bits, sizeof(out.value));
  return true;
}

}  // namespace kart
