// Host-side tests for the drive state machine (the safety core).
#include <unity.h>

#include "drive_state.h"

using kart::DriveInputs;
using kart::DriveOutputs;
using kart::DriveState;
using kart::DriveStateMachine;
using kart::FaultCode;

namespace {

// All-healthy, at-rest inputs with no operator requests. bus_ready reflects
// the traction bus charged with the main contactor closed.
DriveInputs healthy() {
  DriveInputs in{};
  in.wheel_connected = true;
  in.steer_link_ok = true;
  in.steer_calibrated = true;
  in.steer_fault = false;
  in.pedal_plausible = true;
  in.dac_ok = true;
  in.contactor_ok = true;
  in.throttle_at_zero = true;
  in.vehicle_stopped = true;
  in.bus_ready = true;
  return in;
}

// Drives a machine from SAFE into DRIVE. Time ends at t=200.
void enter_drive(DriveStateMachine &m) {
  DriveInputs in = healthy();
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());
  in.arm_confirmed = false;
  in.drive_requested = true;
  m.tick(in, 200);
  TEST_ASSERT_EQUAL((int)DriveState::kDrive, (int)m.state());
}

}  // namespace

void setUp() {}
void tearDown() {}

// -------------------- boot / SAFE --------------------

void test_boots_safe_all_outputs_off() {
  DriveStateMachine m;
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
  DriveOutputs out = m.outputs();
  TEST_ASSERT_FALSE(out.contactor_closed);
  TEST_ASSERT_FALSE(out.steer_enable);
  TEST_ASSERT_FALSE(out.throttle_allowed);
  TEST_ASSERT_FALSE(out.brake_assert);
}

void test_no_arm_without_confirmation() {
  DriveStateMachine m;
  m.tick(healthy(), 100);
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
}

void test_no_arm_when_unhealthy() {
  struct Case {
    void (*mutate)(DriveInputs &);
  } cases[] = {
      {[](DriveInputs &in) { in.wheel_connected = false; }},
      {[](DriveInputs &in) { in.steer_link_ok = false; }},
      {[](DriveInputs &in) { in.steer_calibrated = false; }},
      {[](DriveInputs &in) { in.steer_fault = true; }},
      {[](DriveInputs &in) { in.pedal_plausible = false; }},
      {[](DriveInputs &in) { in.contactor_ok = false; }},
      {[](DriveInputs &in) { in.throttle_at_zero = false; }},
      {[](DriveInputs &in) { in.vehicle_stopped = false; }},
  };
  for (auto &c : cases) {
    DriveStateMachine m;
    DriveInputs in = healthy();
    in.arm_confirmed = true;
    c.mutate(in);
    m.tick(in, 100);
    TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
  }
}

// -------------------- ARMED --------------------

void test_arm_then_drive() {
  DriveStateMachine m;
  enter_drive(m);
  DriveOutputs out = m.outputs();
  TEST_ASSERT_TRUE(out.contactor_closed);
  TEST_ASSERT_TRUE(out.steer_enable);
  TEST_ASSERT_TRUE(out.throttle_allowed);
  TEST_ASSERT_FALSE(out.brake_assert);
}

void test_armed_outputs_no_throttle() {
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.arm_confirmed = true;
  m.tick(in, 100);
  DriveOutputs out = m.outputs();
  TEST_ASSERT_TRUE(out.contactor_closed);
  TEST_ASSERT_TRUE(out.steer_enable);
  TEST_ASSERT_FALSE(out.throttle_allowed);
}

void test_armed_holds_indefinitely_without_timeout() {
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());

  // No inactivity timeout: ARMED (contactor closed) persists indefinitely as
  // long as it stays healthy and nobody disarms. Well past the old 30 s window.
  in.arm_confirmed = false;
  m.tick(in, 100 + 300000);  // +5 minutes
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());
  TEST_ASSERT_TRUE(m.outputs().contactor_closed);

  // A DISARM still cleanly returns to SAFE with no latched fault.
  in.disarm_requested = true;
  m.tick(in, 100 + 300001);
  m.tick(in, 100 + 300002);
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kNone, (int)m.fault());
}

void test_no_drive_entry_with_throttle_pressed() {
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.arm_confirmed = true;
  m.tick(in, 100);
  in.arm_confirmed = false;
  in.drive_requested = true;
  in.throttle_at_zero = false;
  m.tick(in, 200);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());
}

void test_no_drive_entry_until_bus_ready() {
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());

  // Contactor still sequencing: bus not yet charged -> stay ARMED.
  in.arm_confirmed = false;
  in.drive_requested = true;
  in.bus_ready = false;
  m.tick(in, 200);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());

  // Bus comes up -> DRIVE.
  in.bus_ready = true;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kDrive, (int)m.state());
}

// The DAC is powered by the ESC (key on, after the contactor closes), so it is
// dead while SAFE/ARMED: it must NOT block arming, only DRIVE entry.
void test_dac_not_required_to_arm_but_gates_drive() {
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.dac_ok = false;  // ESC not keyed yet -> DAC unpowered
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());  // armed anyway

  // Request DRIVE with the DAC still dead -> stays ARMED.
  in.arm_confirmed = false;
  in.drive_requested = true;
  m.tick(in, 200);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());

  // Operator turns the key -> DAC alive -> DRIVE.
  in.dac_ok = true;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kDrive, (int)m.state());
}

void test_dac_loss_in_drive_faults_only_under_throttle() {
  DriveStateMachine m;
  enter_drive(m);
  DriveInputs in = healthy();
  in.dac_ok = false;
  in.throttle_at_zero = false;  // throttle commanded -> DAC matters
  in.vehicle_stopped = false;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kDacError, (int)m.fault());
}

void test_dac_loss_with_throttle_released_does_not_fault() {
  // Turning the ESC key off at a stop unpowers the DAC; with the throttle
  // released this must stay in DRIVE, not latch a fault.
  DriveStateMachine m;
  enter_drive(m);
  DriveInputs in = healthy();
  in.dac_ok = false;
  in.throttle_at_zero = true;  // throttle released
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kDrive, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kNone, (int)m.fault());
}

// -------------------- traction-only bench mode (§3.6) --------------------

void test_traction_only_bench_skips_steering_health() {
  DriveStateMachine m;
  m.set_traction_only_bench(true);
  DriveInputs in = healthy();
  // Steervo absent / faulted in every way — irrelevant on stands.
  in.steer_link_ok = false;
  in.steer_calibrated = false;
  in.steer_fault = true;
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());

  in.arm_confirmed = false;
  in.drive_requested = true;
  m.tick(in, 200);
  TEST_ASSERT_EQUAL((int)DriveState::kDrive, (int)m.state());
}

void test_traction_only_bench_still_enforces_other_health() {
  DriveStateMachine m;
  m.set_traction_only_bench(true);
  DriveInputs in = healthy();
  in.steer_link_ok = false;  // suppressed
  in.wheel_connected = false;  // NOT suppressed
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
}

void test_normal_mode_still_blocks_on_steering() {
  // Default (non-bench) build keeps the full steering-health gate.
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.steer_link_ok = false;
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
}

// -------------------- contactor fault --------------------

void test_contactor_fault_in_drive_causes_stop_and_latch() {
  DriveStateMachine m;
  enter_drive(m);
  DriveInputs in = healthy();
  in.contactor_ok = false;
  in.vehicle_stopped = false;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kContactorFault, (int)m.fault());
  in.vehicle_stopped = true;
  m.tick(in, 400);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kContactorFault, (int)m.fault());
}

// -------------------- DRIVE faults --------------------

void test_wheel_loss_in_drive_causes_controlled_stop_then_latched_fault() {
  DriveStateMachine m;
  enter_drive(m);

  DriveInputs in = healthy();
  in.wheel_connected = false;
  in.vehicle_stopped = false;  // moving
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kWheelLost, (int)m.fault());

  DriveOutputs out = m.outputs();
  TEST_ASSERT_TRUE(out.contactor_closed);   // ESC still powered to brake
  TEST_ASSERT_TRUE(out.brake_assert);
  TEST_ASSERT_TRUE(out.steer_enable);       // driver keeps steering
  TEST_ASSERT_FALSE(out.throttle_allowed);

  // Comes to rest -> latched FAULT, everything off.
  in.vehicle_stopped = true;
  m.tick(in, 400);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kWheelLost, (int)m.fault());
  out = m.outputs();
  TEST_ASSERT_FALSE(out.contactor_closed);
  TEST_ASSERT_FALSE(out.steer_enable);
  TEST_ASSERT_FALSE(out.brake_assert);
}

void test_stopping_cap_forces_fault_even_if_never_stopped() {
  DriveStateMachine m;
  enter_drive(m);

  DriveInputs in = healthy();
  in.steer_link_ok = false;
  in.vehicle_stopped = false;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());

  // Still "moving" at the cap (e.g. hall sensor itself is suspect).
  m.tick(in, 300 + DriveStateMachine::kStoppingCapMs);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kSteerTimeout, (int)m.fault());
  TEST_ASSERT_FALSE(m.outputs().contactor_closed);
}

void test_disarm_in_drive_is_clean_stop_no_fault() {
  DriveStateMachine m;
  enter_drive(m);

  DriveInputs in = healthy();
  in.disarm_requested = true;
  in.vehicle_stopped = false;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kNone, (int)m.fault());

  in.disarm_requested = false;
  in.vehicle_stopped = true;
  m.tick(in, 400);
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
}

void test_health_failure_during_clean_stop_upgrades_to_fault() {
  DriveStateMachine m;
  enter_drive(m);

  DriveInputs in = healthy();
  in.disarm_requested = true;
  in.vehicle_stopped = false;
  m.tick(in, 300);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());

  in.disarm_requested = false;
  in.steer_fault = true;  // Steervo faults mid-stop
  m.tick(in, 350);
  in.vehicle_stopped = true;
  m.tick(in, 400);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kSteerFault, (int)m.fault());
}

// -------------------- FAULT recovery --------------------

void test_fault_clear_requires_request_rest_and_health() {
  DriveStateMachine m;
  enter_drive(m);
  DriveInputs in = healthy();
  in.wheel_connected = false;
  m.tick(in, 300);   // stopping (already at rest -> next tick faults)
  m.tick(in, 310);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());

  // Clear refused while the cause persists.
  in.fault_clear_requested = true;
  m.tick(in, 400);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());

  // Clear refused while moving.
  in.wheel_connected = true;
  in.vehicle_stopped = false;
  m.tick(in, 500);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());

  // Healthy + at rest + requested -> SAFE.
  in.vehicle_stopped = true;
  m.tick(in, 600);
  TEST_ASSERT_EQUAL((int)DriveState::kSafe, (int)m.state());
  TEST_ASSERT_EQUAL((int)FaultCode::kNone, (int)m.fault());
}

void test_no_spontaneous_clear_without_request() {
  DriveStateMachine m;
  enter_drive(m);
  DriveInputs in = healthy();
  in.dac_ok = false;
  in.throttle_at_zero = false;  // throttle commanded so DAC loss faults
  m.tick(in, 300);
  m.tick(in, 310);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());

  in.dac_ok = true;  // cause resolved but no clear request
  m.tick(in, 400);
  TEST_ASSERT_EQUAL((int)DriveState::kFault, (int)m.state());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_boots_safe_all_outputs_off);
  RUN_TEST(test_no_arm_without_confirmation);
  RUN_TEST(test_no_arm_when_unhealthy);
  RUN_TEST(test_arm_then_drive);
  RUN_TEST(test_armed_outputs_no_throttle);
  RUN_TEST(test_armed_holds_indefinitely_without_timeout);
  RUN_TEST(test_no_drive_entry_with_throttle_pressed);
  RUN_TEST(test_no_drive_entry_until_bus_ready);
  RUN_TEST(test_dac_not_required_to_arm_but_gates_drive);
  RUN_TEST(test_dac_loss_in_drive_faults_only_under_throttle);
  RUN_TEST(test_dac_loss_with_throttle_released_does_not_fault);
  RUN_TEST(test_traction_only_bench_skips_steering_health);
  RUN_TEST(test_traction_only_bench_still_enforces_other_health);
  RUN_TEST(test_normal_mode_still_blocks_on_steering);
  RUN_TEST(test_contactor_fault_in_drive_causes_stop_and_latch);
  RUN_TEST(test_wheel_loss_in_drive_causes_controlled_stop_then_latched_fault);
  RUN_TEST(test_stopping_cap_forces_fault_even_if_never_stopped);
  RUN_TEST(test_disarm_in_drive_is_clean_stop_no_fault);
  RUN_TEST(test_health_failure_during_clean_stop_upgrades_to_fault);
  RUN_TEST(test_fault_clear_requires_request_rest_and_health);
  RUN_TEST(test_no_spontaneous_clear_without_request);
  return UNITY_END();
}
