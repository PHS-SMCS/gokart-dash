// Drive state machine — the safety core of kart-core.
// Spec: docs/SOFTWARE-STACK-PLAN.md §3.1 and docs/protocols/uart-protocol.md.
//
// Pure C++ (no Arduino) so it is unit-tested on the host. The caller owns all
// I/O: it gathers DriveInputs each tick and applies DriveOutputs to hardware.
#pragma once

#include <stdint.h>

namespace kart {

enum class DriveState : uint8_t {
  kSafe = 0,
  kArmed = 1,
  kDrive = 2,
  kStopping = 3,  // controlled stop in progress
  kFault = 4,     // latched; requires explicit clear
};

enum class FaultCode : uint8_t {
  kNone = 0,
  kWheelLost = 1,
  kSteerTimeout = 2,
  kSteerFault = 3,
  kPedalImplausible = 4,
  kDacError = 5,
  kArmedTimeout = 6,  // informational only — never latched
  kInternalWdt = 7,
  kContactorFault = 8,  // contactor/bus sequencing reported a fault
};

struct DriveInputs {
  // Subsystem health
  bool wheel_connected;
  bool steer_link_ok;
  bool steer_calibrated;
  bool steer_fault;  // Steervo reports FAULT state
  bool pedal_plausible;
  bool dac_ok;
  bool contactor_ok;  // contactor sequencer healthy (no precharge/seq fault)

  // Vehicle condition
  bool throttle_at_zero;
  bool vehicle_stopped;  // hall pulse frequency == 0
  bool bus_ready;        // traction bus charged + main contactor closed

  // Operator requests (edge semantics owned by the caller)
  bool arm_confirmed;   // driver wheel chord completed
  bool drive_requested;
  bool disarm_requested;
  bool fault_clear_requested;
};

struct DriveOutputs {
  bool contactor_closed;
  bool steer_enable;     // ENABLE bit in STEER_SET
  bool throttle_allowed; // pedal -> DAC path live
  bool brake_assert;     // force brake regardless of pedal
};

class DriveStateMachine {
 public:
  // ARMED has no inactivity timeout: it holds (contactor closed) until it
  // auto-advances to DRIVE, or the driver DISARMs, or a health fault trips a
  // controlled stop. The operator turns the ESC key and precharges during
  // ARMED (which has no bounded duration); once the bus is ready and the DAC is
  // alive, DRIVE follows automatically — there is no separate "go" button. A
  // surprise contactor drop mid-setup was worse than an indefinite hold (e-stop
  // + DISARM always cut it).
  static constexpr uint32_t kStoppingCapMs = 3000;

  // Traction-only bench mode (spec §3.6): stands-only configuration that
  // removes *only* the steering-health DRIVE-entry condition and suppresses
  // the two Steervo watchdog faults. Everything else (pedal plausibility,
  // contactor sequencing, hall-speed-zero entry, brake override, controlled
  // stop on every other fault) is unchanged. Set once at construction from a
  // compile-time flag; never the default build, never a ground-driving image.
  void set_traction_only_bench(bool on) { traction_only_bench_ = on; }
  bool traction_only_bench() const { return traction_only_bench_; }

  // Advance the machine. `now_ms` must be monotonic.
  void tick(const DriveInputs &in, uint32_t now_ms);

  DriveState state() const { return state_; }

  // Latched fault while in kFault; the pending fault while in kStopping;
  // kNone otherwise.
  FaultCode fault() const;

  DriveOutputs outputs() const;

 private:
  FaultCode health_fault(const DriveInputs &in) const;
  void begin_stop(FaultCode pending, uint32_t now_ms);

  bool traction_only_bench_ = false;
  DriveState state_ = DriveState::kSafe;
  FaultCode latched_fault_ = FaultCode::kNone;
  FaultCode pending_fault_ = FaultCode::kNone;
  uint32_t stopping_since_ms_ = 0;
};

}  // namespace kart
