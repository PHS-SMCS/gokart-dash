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

  // A stall fault is self-clearing: after a cooldown, drop it and let the loop
  // retry. A jam that frees (or a transient) must never leave the steering
  // latched off (that used to require an ESP32 reset); if it is still stalled it
  // simply re-trips after another stall_timeout, a bounded on/off duty. Pot-range
  // stays hard-latched — an implausible reading is a real sensor failure.
  if (stall_fault_ &&
      (uint32_t)(now_ms - stall_since_ms_) >= cfg_.stall_recover_ms) {
    stall_fault_ = false;
    stall_window_open_ = false;
    pid_.reset();
  }

  // Pot plausibility dominates everything: without trustworthy feedback the
  // motor must never run.
  if (!pot_plausible(pot_raw)) {
    pot_range_fault_ = true;
  }

  if (cal_.valid) {
    measured_cdeg_ = pot_to_angle_cdeg(cal_, pot_raw);
  }

  // Over-travel: the steering is past a calibrated end stop. This is NOT a fault
  // and never latches the motor off. A momentary overshoot at full lock, a touch
  // of oscillation, pot noise, or a hand-push past the stop would otherwise trip
  // a single-tick latch and strand the steering at the edge, unable to return to
  // centre (the reported bug). Instead the output clamp below permits motion only
  // toward centre while past a stop — the motor can always drive itself back out,
  // but can never be commanded deeper into the rail. Genuine sensor failure is
  // still caught by the pot-range fault; a wrong-way runaway by the convergence
  // watchdog (which trips mid-travel, before a stop is reached).
  bool over_travel_now = raw_over_travel(pot_raw);

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

  // Allowed to run even while past a stop — the output clamp (below) restricts
  // motion to the recovery direction, so it can drive itself back to centre.
  bool want_active = enable_ && fresh && cal_.valid;
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

  // Over-travel recovery clamp: while past a stop, permit motion ONLY toward
  // centre and never further into the stop. measured_cdeg_ is clamped at the
  // stop angle here, so its sign tells us which stop; positive output drives
  // the measurement toward the (interior) target. This guarantees the motor
  // can unstick itself but can never be commanded deeper into the rail — even
  // if the loop sign were somehow wrong.
  if (over_travel_now) {
    if (measured_cdeg_ < 0) {          // past the left stop: only positive (toward centre)
      if (out < 0.0f) out = 0.0f;
    } else {                            // past the right stop: only negative
      if (out > 0.0f) out = 0.0f;
    }
  }

  // Convergence watchdog: while the motor pushes hard, the POT must move in the
  // commanded direction. Progress is measured on the pot position, NOT on the
  // error — this is what makes fast wheel wiggling safe. When the setpoint races
  // back and forth the error never settles, but the pot IS being driven correctly,
  // so it is not a stall. A genuine JAM (pot frozen under a hard push) or a
  // wrong-way RUNAWAY (inverted feedback: pot moving opposite the command) fails
  // to make pot progress and trips after stall_timeout_ms — catching a runaway
  // before it reaches a stop, on top of the over-travel clamp above. The window
  // also restarts on a command-direction flip (a wiggle reversal).
  float out_dir = (out >= 0.0f) ? 1.0f : -1.0f;
  bool pushing = (out > cfg_.stall_output_frac * cfg_.output_limit) ||
                 (out < -cfg_.stall_output_frac * cfg_.output_limit);
  if (pushing) {
    int32_t progress =
        stall_window_open_
            ? (int32_t)((float)(measured_cdeg_ - stall_window_start_meas_) * out_dir)
            : 0;
    bool dir_flipped = stall_window_open_ && out_dir != stall_window_dir_;
    if (!stall_window_open_ || dir_flipped ||
        progress >= cfg_.stall_min_delta_cdeg) {
      // Fresh push, a wiggle reversal, or real pot progress toward the command:
      // the loop is doing its job. (Re)open the window from here.
      stall_window_open_ = true;
      stall_window_start_ms_ = now_ms;
      stall_window_start_meas_ = measured_cdeg_;
      stall_window_dir_ = out_dir;
    } else if ((uint32_t)(now_ms - stall_window_start_ms_) >=
               cfg_.stall_timeout_ms) {
      stall_fault_ = true;
      stall_since_ms_ = now_ms;
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
  // NB: over-travel is no longer a fault (recoverable via the toward-centre
  // clamp), so kSteerFaultOverTravel is intentionally never set here.
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
