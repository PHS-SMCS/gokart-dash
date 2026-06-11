#include "drive_state.h"

namespace kart {

FaultCode DriveStateMachine::health_fault(const DriveInputs &in) {
  // Checked in priority order; the first failure wins.
  if (!in.wheel_connected) return FaultCode::kWheelLost;
  if (!in.steer_link_ok) return FaultCode::kSteerTimeout;
  if (in.steer_fault) return FaultCode::kSteerFault;
  if (!in.pedal_plausible) return FaultCode::kPedalImplausible;
  if (!in.dac_ok) return FaultCode::kDacError;
  return FaultCode::kNone;
}

void DriveStateMachine::begin_stop(FaultCode pending, uint32_t now_ms) {
  pending_fault_ = pending;
  stopping_since_ms_ = now_ms;
  state_ = DriveState::kStopping;
}

void DriveStateMachine::tick(const DriveInputs &in, uint32_t now_ms) {
  switch (state_) {
    case DriveState::kSafe: {
      // Arming requires a deliberate driver action and every subsystem
      // healthy with the kart at rest. steer_calibrated additionally gates
      // arming (an uncalibrated Steervo is READY but must not drive).
      if (in.arm_confirmed && health_fault(in) == FaultCode::kNone &&
          in.steer_calibrated && in.vehicle_stopped && in.throttle_at_zero) {
        armed_since_ms_ = now_ms;
        state_ = DriveState::kArmed;
      }
      break;
    }

    case DriveState::kArmed: {
      FaultCode hf = health_fault(in);
      if (hf != FaultCode::kNone) {
        begin_stop(hf, now_ms);
        break;
      }
      if (in.disarm_requested) {
        begin_stop(FaultCode::kNone, now_ms);
        break;
      }
      if ((uint32_t)(now_ms - armed_since_ms_) >= kArmedTimeoutMs) {
        // Informational timeout (fault code 6 is event-only, never latched).
        begin_stop(FaultCode::kNone, now_ms);
        break;
      }
      if (in.drive_requested && in.throttle_at_zero) {
        state_ = DriveState::kDrive;
      }
      break;
    }

    case DriveState::kDrive: {
      FaultCode hf = health_fault(in);
      if (hf != FaultCode::kNone) {
        begin_stop(hf, now_ms);
        break;
      }
      if (in.disarm_requested) {
        begin_stop(FaultCode::kNone, now_ms);
      }
      break;
    }

    case DriveState::kStopping: {
      // A new health failure during the stop upgrades a plain disarm to a
      // latched fault, but never downgrades an existing pending fault.
      if (pending_fault_ == FaultCode::kNone) {
        pending_fault_ = health_fault(in);
      }
      bool stop_complete =
          in.vehicle_stopped ||
          (uint32_t)(now_ms - stopping_since_ms_) >= kStoppingCapMs;
      if (stop_complete) {
        if (pending_fault_ != FaultCode::kNone) {
          latched_fault_ = pending_fault_;
          pending_fault_ = FaultCode::kNone;
          state_ = DriveState::kFault;
        } else {
          state_ = DriveState::kSafe;
        }
      }
      break;
    }

    case DriveState::kFault: {
      // Clearing requires: explicit request, kart at rest, and the original
      // cause (plus everything else) healthy again.
      if (in.fault_clear_requested && in.vehicle_stopped &&
          health_fault(in) == FaultCode::kNone) {
        latched_fault_ = FaultCode::kNone;
        state_ = DriveState::kSafe;
      }
      break;
    }
  }
}

FaultCode DriveStateMachine::fault() const {
  if (state_ == DriveState::kFault) return latched_fault_;
  if (state_ == DriveState::kStopping) return pending_fault_;
  return FaultCode::kNone;
}

DriveOutputs DriveStateMachine::outputs() const {
  DriveOutputs out{false, false, false, false};
  switch (state_) {
    case DriveState::kSafe:
    case DriveState::kFault:
      // Everything de-energized. The kart is at rest (or the stopping cap
      // expired); the contactor is open.
      break;
    case DriveState::kArmed:
      out.contactor_closed = true;
      out.steer_enable = true;
      break;
    case DriveState::kDrive:
      out.contactor_closed = true;
      out.steer_enable = true;
      out.throttle_allowed = true;
      break;
    case DriveState::kStopping:
      // Keep the contactor closed so the ESC can brake; driver keeps
      // steering authority for the duration of the stop.
      out.contactor_closed = true;
      out.steer_enable = true;
      out.brake_assert = true;
      break;
  }
  return out;
}

}  // namespace kart
