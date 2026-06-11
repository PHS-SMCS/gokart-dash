// Steervo control core: consumes STEER_SET frames + pot readings, produces a
// Talon output demand and STEER_STATUS heartbeats. Pure C++ (no Arduino) —
// the main sketch owns TWAI, the ADC, and NVS.
//
// Safety properties (unit-tested):
//   - No motor output unless ACTIVE (calibrated + enabled + fresh setpoints).
//   - Setpoint staleness (>150 ms) drops to READY with motor off.
//   - Implausible pot or stall is a hard, latched fault: motor off.
//   - Setpoints are clamped inside the calibrated range minus a margin.
#pragma once

#include <stdint.h>

#include "kart_can.h"
#include "pid.h"
#include "pot_map.h"

namespace steervo {

struct SteerConfig {
  // PID gains: output fraction per centi-degree of error. Bench-tuned in
  // Phase 1; defaults are deliberately weak.
  float kp = 0.002f;
  float ki = 0.0f;
  float kd = 0.0f;
  float output_limit = 0.4f;  // fraction of full Talon output, conservative
  int16_t soft_limit_margin_cdeg = 100;
  uint32_t setpoint_timeout_ms = kart::kSteerSetTimeoutMs;
  uint32_t stall_timeout_ms = 800;
  float stall_output_frac = 0.9f;   // of output_limit
  int16_t stall_min_delta_cdeg = 25;
};

class SteerController {
 public:
  explicit SteerController(const SteerConfig &cfg = SteerConfig())
      : cfg_(cfg), pid_(cfg.kp, cfg.ki, cfg.kd, cfg.output_limit) {}

  void set_calibration(const PotCalibration &cal) { cal_ = cal; }
  const PotCalibration &calibration() const { return cal_; }

  void on_steer_set(const kart::SteerSet &msg, uint32_t now_ms);
  void on_cfg(const kart::SteerCfg &msg);

  // Reported by the transport layer (e.g. TWAI TX failures / bus-off).
  void set_talon_lost(bool lost) { talon_lost_ = lost; }

  // Advance one control step. Returns the motor demand as a fraction in
  // [-output_limit, +output_limit]; exactly 0.0 unless state is ACTIVE.
  float tick(uint32_t now_ms, uint16_t pot_raw);

  kart::SteerState state() const { return state_; }
  uint8_t fault_bits() const;
  kart::SteerStatus status() const;
  int16_t measured_cdeg() const { return measured_cdeg_; }

 private:
  bool hard_faulted() const { return pot_range_fault_ || stall_fault_; }
  int16_t clamp_to_soft_limits(int16_t setpoint_cdeg) const;

  SteerConfig cfg_;
  Pid pid_;
  PotCalibration cal_{0, 0, 0, 0, 0, false};

  kart::SteerState state_ = kart::SteerState::kInit;

  // Latched hard faults (power cycle to clear — TODO(phase-1): explicit
  // fault-clear path via STEER_CAL).
  bool pot_range_fault_ = false;
  bool stall_fault_ = false;
  // Soft conditions
  bool setpoint_stale_ = false;
  bool talon_lost_ = false;

  bool have_setpoint_ = false;
  bool enable_ = false;
  int16_t setpoint_cdeg_ = 0;
  uint8_t seq_echo_ = 0;
  uint32_t last_set_ms_ = 0;

  uint16_t pot_raw_ = 0;
  int16_t measured_cdeg_ = 0;
  float last_output_ = 0.0f;

  // Stall detection window
  bool stall_window_open_ = false;
  uint32_t stall_window_start_ms_ = 0;
  int16_t stall_window_start_cdeg_ = 0;
};

}  // namespace steervo
