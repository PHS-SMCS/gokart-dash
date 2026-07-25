#include "drive_state.h"

namespace kart {

FaultCode DriveStateMachine::health_fault(const DriveInputs &in) const {
  // Checked in priority order; the first failure wins. In traction-only bench
  // mode (§3.6) the two Steervo watchdog faults are suppressed — steering is
  // declared absent, not a fault.
  if (!in.wheel_connected) return FaultCode::kWheelLost;
  if (!traction_only_bench_) {
    if (!in.steer_link_ok) return FaultCode::kSteerTimeout;
    if (in.steer_fault) return FaultCode::kSteerFault;
  }
  if (!in.pedal_plausible) return FaultCode::kPedalImplausible;
  if (!in.contactor_ok) return FaultCode::kContactorFault;
  // NOTE: dac_ok is deliberately NOT a general health condition. The MCP4725
  // is powered from the ESC's ACC+ rail, which only comes up after the key is
  // turned — and the key is turned *after* the contactor closes (i.e. after
  // ARM). So the DAC is necessarily dead while SAFE/ARMED; requiring it to arm
  // would deadlock. dac_ok is instead a DRIVE-entry gate and a DRIVE-only
  // fault (see tick()), since the throttle DAC only matters once driving.
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
      // arming (an uncalibrated Steervo is READY but must not drive) —
      // suppressed in traction-only bench mode, where steering is absent.
      bool steer_ready = traction_only_bench_ || in.steer_calibrated;
      if (in.arm_confirmed && health_fault(in) == FaultCode::kNone &&
          steer_ready && in.vehicle_stopped && in.throttle_at_zero) {
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
      // No ARMED inactivity timeout: the contactor stays closed in ARMED until
      // the driver enters DRIVE or explicitly disarms (or a health fault trips
      // a controlled stop). The operator turns the ESC key and precharges the
      // bus during ARMED, which can take longer than any fixed window; a
      // surprise contactor drop mid-setup was worse than an indefinite hold on
      // stands with the e-stop and DISARM always available.
      // DRIVE entry waits for the traction bus (software precharge done, main
      // contactor closed, settle elapsed — all via bus_ready) AND for the DAC
      // to be alive — by now the operator has turned the key, powering the
      // ESC and the throttle DAC. No throttle is ever commanded without dac_ok.
      if (in.drive_requested && in.throttle_at_zero && in.bus_ready &&
          in.dac_ok) {
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
      // DAC must stay alive only while throttle is actually being commanded.
      // With the throttle released we're commanding zero, so DAC loss is
      // harmless and must NOT fault — e.g. turning the ESC key off at a stop
      // (which unpowers the ACC+-fed DAC) should not latch a fault.
      if (!in.dac_ok && !in.throttle_at_zero) {
        begin_stop(FaultCode::kDacError, now_ms);
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
