#include "steer_controller.h"

namespace steervo {

void SteerController::on_steer_set(const kart::SteerSet &msg,
                                   uint32_t now_ms) {
  have_setpoint_ = true;
  enable_ = msg.enable;
  setpoint_cdeg_ = msg.setpoint_cdeg;
  seq_echo_ = msg.seq;
  last_set_ms_ = now_ms;
  setpoint_stale_ = false;
}

void SteerController::on_cfg(const kart::SteerCfg &msg) {
  // Bench tuning only; the main sketch must not forward CFG while ACTIVE.
  switch (msg.param) {
    case kart::SteerCfgParam::kKp:
      cfg_.kp = msg.value;
      break;
    case kart::SteerCfgParam::kKi:
      cfg_.ki = msg.value;
      break;
    case kart::SteerCfgParam::kKd:
      cfg_.kd = msg.value;
      break;
    case kart::SteerCfgParam::kOutputLimitPct:
      cfg_.output_limit = msg.value / 100.0f;
      pid_.set_output_limit(cfg_.output_limit);
      break;
    case kart::SteerCfgParam::kSoftLimitMarginCdeg:
      cfg_.soft_limit_margin_cdeg = (int16_t)msg.value;
      break;
  }
  pid_.set_gains(cfg_.kp, cfg_.ki, cfg_.kd);
}

bool SteerController::on_cal(kart::SteerCalCmd cmd, uint16_t pot_raw) {
  switch (cmd) {
    case kart::SteerCalCmd::kEnter:
      // Don't begin a calibration on top of an unrecoverable hard fault.
      if (hard_faulted()) break;
      state_ = kart::SteerState::kCalibrating;
      cal_capture_ = PotCalibration{0, 0, 0, 0, 0, false};
      cap_center_ = cap_left_ = cap_right_ = false;
      enable_ = false;
      break;
    case kart::SteerCalCmd::kMarkCenter:
      if (state_ == kart::SteerState::kCalibrating) {
        cal_capture_.raw_center = pot_raw;
        cap_center_ = true;
      }
      break;
    case kart::SteerCalCmd::kMarkLeft:
      if (state_ == kart::SteerState::kCalibrating) {
        cal_capture_.raw_left = pot_raw;
        cap_left_ = true;
      }
      break;
    case kart::SteerCalCmd::kMarkRight:
      if (state_ == kart::SteerState::kCalibrating) {
        cal_capture_.raw_right = pot_raw;
        cap_right_ = true;
      }
      break;
    case kart::SteerCalCmd::kSaveExit:
      if (state_ == kart::SteerState::kCalibrating && cap_center_ &&
          cap_left_ && cap_right_) {
        cal_capture_.angle_left_cdeg = (int16_t)-kart::kSteerRangeCdeg;
        cal_capture_.angle_right_cdeg = (int16_t)kart::kSteerRangeCdeg;
        cal_capture_.valid = true;
        cal_ = cal_capture_;
        state_ = kart::SteerState::kReady;
        return true;  // caller persists cal_ to NVS
      }
      break;  // incomplete: stay in CALIBRATING
    case kart::SteerCalCmd::kAbort:
      if (state_ == kart::SteerState::kCalibrating) {
        state_ = cal_.valid ? kart::SteerState::kReady : kart::SteerState::kInit;
      }
      break;
  }
  return false;
}

bool SteerController::raw_over_travel(uint16_t raw) const {
  if (!cal_.valid) return false;
  int32_t lo = cal_.raw_left < cal_.raw_right ? cal_.raw_left : cal_.raw_right;
  int32_t hi = cal_.raw_left < cal_.raw_right ? cal_.raw_right : cal_.raw_left;
  int32_t m = cfg_.over_travel_margin_raw;
  return (int32_t)raw < lo - m || (int32_t)raw > hi + m;
}

int16_t SteerController::clamp_to_soft_limits(int16_t setpoint_cdeg) const {
  int32_t lo = cal_.angle_left_cdeg + cfg_.soft_limit_margin_cdeg;
  int32_t hi = cal_.angle_right_cdeg - cfg_.soft_limit_margin_cdeg;
  if (setpoint_cdeg < lo) return (int16_t)lo;
  if (setpoint_cdeg > hi) return (int16_t)hi;
  return setpoint_cdeg;
}

float SteerController::tick(uint32_t now_ms, uint16_t pot_raw) {
  pot_raw_ = pot_raw;

  // Pot plausibility dominates everything: without trustworthy feedback the
  // motor must never run.
  if (!pot_plausible(pot_raw)) {
    pot_range_fault_ = true;
  }

  if (cal_.valid) {
    measured_cdeg_ = pot_to_angle_cdeg(cal_, pot_raw);
  }

  // Over-travel: the steering went past a calibrated end stop. If the motor was
  // actively driving, this is a hard latched fault (it should have held inside
  // the soft limits) — cut power and require inspection. If the motor was not
  // driving (e.g. hand-moved during setup), don't latch, but block activation
  // below until the pot comes back inside range.
  bool over_travel_now = raw_over_travel(pot_raw);
  if (over_travel_now && state_ == kart::SteerState::kActive) {
    over_travel_fault_ = true;
  }

  if (hard_faulted()) {
    state_ = kart::SteerState::kFault;
    last_output_ = 0.0f;
    pid_.reset();
    stall_window_open_ = false;
    return 0.0f;
  }

  if (state_ == kart::SteerState::kInit) {
    state_ = kart::SteerState::kReady;  // first plausible pot reading
  }

  if (state_ == kart::SteerState::kCalibrating) {
    last_output_ = 0.0f;
    return 0.0f;
  }

  // Staleness: an enabled link that stops producing frames de-energizes the
  // motor (docs/protocols/can-ids.md).
  bool fresh = have_setpoint_ &&
               (uint32_t)(now_ms - last_set_ms_) < cfg_.setpoint_timeout_ms;
  if (!fresh) {
    if (have_setpoint_ && enable_) {
      setpoint_stale_ = true;
    }
    enable_ = false;
  }

  // Never (re)start the motor while the pot is sitting past a stop.
  bool want_active = enable_ && fresh && cal_.valid && !over_travel_now;
  if (!want_active) {
    if (state_ == kart::SteerState::kActive) {
      pid_.reset();
      stall_window_open_ = false;
    }
    state_ = kart::SteerState::kReady;
    last_output_ = 0.0f;
    return 0.0f;
  }

  state_ = kart::SteerState::kActive;

  int16_t target = clamp_to_soft_limits(setpoint_cdeg_);
  float error = (float)(target - measured_cdeg_);
  // Caller runs a fixed 100 Hz tick. Pass the measurement so the PID takes its
  // derivative on the pot (not the error) — no kick when the wheel setpoint moves.
  float out = pid_.update(error, (float)measured_cdeg_, 10);

  // Convergence watchdog: while pushing hard the |error| must keep shrinking.
  // A jammed motor (no movement) holds |error| constant; a wrong-way runaway
  // (e.g. inverted feedback sign / swapped motor leads) grows it. Either way,
  // if we push for stall_timeout_ms without making stall_min_delta_cdeg of
  // progress, fault and cut the motor — this catches a runaway *before* it
  // reaches a stop, in addition to the over-travel guard above.
  int32_t abs_err = error < 0.0f ? (int32_t)-error : (int32_t)error;
  bool pushing = (out > cfg_.stall_output_frac * cfg_.output_limit) ||
                 (out < -cfg_.stall_output_frac * cfg_.output_limit);
  if (pushing) {
    if (!stall_window_open_) {
      stall_window_open_ = true;
      stall_window_start_ms_ = now_ms;
      stall_window_start_abserr_ = abs_err;
    } else if (stall_window_start_abserr_ - abs_err >=
               cfg_.stall_min_delta_cdeg) {
      // Error is shrinking: the loop is converging. Restart the window.
      stall_window_start_ms_ = now_ms;
      stall_window_start_abserr_ = abs_err;
    } else if ((uint32_t)(now_ms - stall_window_start_ms_) >=
               cfg_.stall_timeout_ms) {
      stall_fault_ = true;
      state_ = kart::SteerState::kFault;
      pid_.reset();
      stall_window_open_ = false;
      last_output_ = 0.0f;
      return 0.0f;
    }
  } else {
    stall_window_open_ = false;
  }

  last_output_ = out;
  return out;
}

uint8_t SteerController::fault_bits() const {
  uint8_t bits = 0;
  if (pot_range_fault_) bits |= kart::kSteerFaultPotRange;
  if (stall_fault_) bits |= kart::kSteerFaultStall;
  if (over_travel_fault_) bits |= kart::kSteerFaultOverTravel;
  if (setpoint_stale_) bits |= kart::kSteerFaultSetpointStale;
  if (talon_lost_) bits |= kart::kSteerFaultTalonLost;
  if (!cal_.valid) bits |= kart::kSteerFaultNotCalibrated;
  return bits;
}

kart::SteerStatus SteerController::status() const {
  kart::SteerStatus s{};
  s.state = state_;
  s.fault_bits = fault_bits();
  s.measured_cdeg = measured_cdeg_;
  float pct = last_output_ * 100.0f;
  if (pct > 100.0f) pct = 100.0f;
  if (pct < -100.0f) pct = -100.0f;
  s.output_pct = (int8_t)pct;
  s.seq_echo = seq_echo_;
  s.pot_raw = pot_raw_;
  return s;
}

}  // namespace steervo
