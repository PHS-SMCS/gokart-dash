// Steering link helper (Teensy side of the Teensy<->Steervo CAN link).
//
// Pure C++ (no Arduino) so it is host-tested. Two jobs:
//   1. Map the Hori wheel axis to a steering setpoint in centi-degrees.
//   2. Track inbound STEER_STATUS heartbeats and expose the Steervo health the
//      drive state machine consumes (link fresh / calibrated / faulted).
//
// The setpoint scale matches kart::kSteerRangeCdeg, which the Steervo also
// assigns to its calibrated left/right pot stops — so wheel full-lock commands
// the physical stop the pot was calibrated against.
#pragma once

#include <stdint.h>

#include "kart_can.h"

namespace kart {

struct SteerLinkConfig {
  int16_t range_cdeg = kSteerRangeCdeg;  // setpoint at full wheel deflection
  int32_t axis_full = 32767;             // |axis| reported at full lock
  int16_t center_deadband_cdeg = 30;     // ±0.30° snapped to center
  uint32_t status_timeout_ms = kSteerStatusTimeoutMs;
};

class SteerLink {
 public:
  explicit SteerLink(const SteerLinkConfig &cfg = SteerLinkConfig())
      : cfg_(cfg) {}

  // Wheel axis (signed, + = right) -> steering setpoint (centi-degrees).
  int16_t axis_to_setpoint(int32_t axis) const {
    if (axis > cfg_.axis_full) axis = cfg_.axis_full;
    if (axis < -cfg_.axis_full) axis = -cfg_.axis_full;
    int32_t cdeg = axis * (int32_t)cfg_.range_cdeg / cfg_.axis_full;
    if (cdeg > -(int32_t)cfg_.center_deadband_cdeg &&
        cdeg < (int32_t)cfg_.center_deadband_cdeg) {
      cdeg = 0;
    }
    return (int16_t)cdeg;
  }

  // Record an inbound STEER_STATUS frame (already unpacked).
  void on_status(const SteerStatus &s, uint32_t now_ms) {
    last_ = s;
    have_status_ = true;
    last_status_ms_ = now_ms;
  }

  bool have_status() const { return have_status_; }

  // Heartbeat fresh within the timeout (safety-critical: no fresh status =>
  // the Teensy treats steering as lost).
  bool link_ok(uint32_t now_ms) const {
    return have_status_ &&
           (uint32_t)(now_ms - last_status_ms_) < cfg_.status_timeout_ms;
  }

  // Steervo reports it has a valid calibration.
  bool calibrated() const {
    return have_status_ && !(last_.fault_bits & kSteerFaultNotCalibrated);
  }

  // Steervo is in its FAULT state.
  bool reports_fault() const {
    return have_status_ && last_.state == SteerState::kFault;
  }

  const SteerStatus &last_status() const { return last_; }

  // Rolling STEER_SET sequence counter.
  uint8_t next_seq() { return seq_++; }

 private:
  SteerLinkConfig cfg_;
  SteerStatus last_{};
  bool have_status_ = false;
  uint32_t last_status_ms_ = 0;
  uint8_t seq_ = 0;
};

}  // namespace kart
