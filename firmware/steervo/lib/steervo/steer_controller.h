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
  // PID gains: output fraction per centi-degree of error (Kd per cdeg/s).
  // Bench-validated (July 2026): a pure-proportional loop is stable, accurate
  // (~0.15 deg steady-state), and oscillation-free even at full output — the
  // 50:1 gearbox's own friction supplies the damping. Kd is deliberately 0:
  // differentiating the noisy pot (±~12 cdeg at rest) over a 10 ms tick injects
  // huge velocity noise that saturates the output and drives a limit cycle.
  // The derivative-on-measurement + filter path (pid.h) is kept for the future,
  // but leave Kd at 0 unless you have a much cleaner velocity source.
  float kp = 0.002f;
  float ki = 0.0f;
  float kd = 0.0f;
  float output_limit = 0.4f;  // fraction of full Talon output, conservative
  int16_t soft_limit_margin_cdeg = 100;
  uint32_t setpoint_timeout_ms = kart::kSteerSetTimeoutMs;
  uint32_t stall_timeout_ms = 800;
  float stall_output_frac = 0.9f;       // of output_limit
  int16_t stall_min_delta_cdeg = 25;    // min error improvement to "still converging"
  // Pot protection: a raw reading this far beyond a calibrated end stop means
  // the steering has been driven past its safe range (the #1 way to rip the
  // pot off its coupling). Caught on the RAW value because pot_to_angle_cdeg
  // clamps the reported angle at the stops and would otherwise hide it.
  uint16_t over_travel_margin_raw = 80;
};

class SteerController {
 public:
  explicit SteerController(const SteerConfig &cfg = SteerConfig())
      : cfg_(cfg), pid_(cfg.kp, cfg.ki, cfg.kd, cfg.output_limit) {}

  void set_calibration(const PotCalibration &cal) { cal_ = cal; }
  const PotCalibration &calibration() const { return cal_; }

  void on_steer_set(const kart::SteerSet &msg, uint32_t now_ms);
  void on_cfg(const kart::SteerCfg &msg);

  // Guided calibration (STEER_CAL). ENTER moves to CALIBRATING; MARK_* capture
  // the current pot reading at the center/left/right mechanical references;
  // SAVE_EXIT commits once all three are marked (assigning ±kSteerRangeCdeg to
  // the left/right stops) and returns true so the caller can persist to NVS;
  // ABORT discards. The motor never runs while CALIBRATING (tick returns 0).
  // Returns true only on a committed SAVE_EXIT.
  bool on_cal(kart::SteerCalCmd cmd, uint16_t pot_raw);

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
  bool hard_faulted() const {
    // Over-travel is deliberately NOT here: it is a recoverable condition handled
    // by the toward-centre output clamp in tick(), not a latched motor-off. See
    // the over-travel comment in tick().
    return pot_range_fault_ || stall_fault_;
  }
  int16_t clamp_to_soft_limits(int16_t setpoint_cdeg) const;
  // True when the raw pot is beyond a calibrated end stop by more than the
  // over-travel margin (only meaningful once calibrated).
  bool raw_over_travel(uint16_t raw) const;

  SteerConfig cfg_;
  Pid pid_;
  PotCalibration cal_{0, 0, 0, 0, 0, false};

  kart::SteerState state_ = kart::SteerState::kInit;

  // Latched hard faults (power cycle to clear — TODO(phase-1): explicit
  // fault-clear path via STEER_CAL). Over-travel is intentionally NOT latched:
  // it self-recovers via the toward-centre output clamp (see tick()).
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

  // Convergence watchdog window: while the motor pushes hard, the |error| must
  // keep shrinking. If it does not (jammed = stall, or growing = wrong-way
  // runaway) for stall_timeout_ms, fault. Tracks the |error| the window opened
  // at; resets whenever the loop makes stall_min_delta_cdeg of progress.
  bool stall_window_open_ = false;
  uint32_t stall_window_start_ms_ = 0;
  int32_t stall_window_start_abserr_ = 0;

  // In-progress calibration capture (committed to cal_ on SAVE_EXIT).
  PotCalibration cal_capture_{0, 0, 0, 0, 0, false};
  bool cap_center_ = false;
  bool cap_left_ = false;
  bool cap_right_ = false;
};

}  // namespace steervo
