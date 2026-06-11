// Host-side tests for the drive state machine (the safety core).
#include <unity.h>

#include "drive_state.h"

using kart::DriveInputs;
using kart::DriveOutputs;
using kart::DriveState;
using kart::DriveStateMachine;
using kart::FaultCode;

namespace {

// All-healthy, at-rest inputs with no operator requests.
DriveInputs healthy() {
  DriveInputs in{};
  in.wheel_connected = true;
  in.steer_link_ok = true;
  in.steer_calibrated = true;
  in.steer_fault = false;
  in.pedal_plausible = true;
  in.dac_ok = true;
  in.throttle_at_zero = true;
  in.vehicle_stopped = true;
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
      {[](DriveInputs &in) { in.dac_ok = false; }},
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

void test_armed_timeout_returns_to_safe_without_latched_fault() {
  DriveStateMachine m;
  DriveInputs in = healthy();
  in.arm_confirmed = true;
  m.tick(in, 100);
  TEST_ASSERT_EQUAL((int)DriveState::kArmed, (int)m.state());

  in.arm_confirmed = false;
  m.tick(in, 100 + DriveStateMachine::kArmedTimeoutMs);
  TEST_ASSERT_EQUAL((int)DriveState::kStopping, (int)m.state());
  // Stationary -> stop completes immediately on the next tick, no fault.
  m.tick(in, 101 + DriveStateMachine::kArmedTimeoutMs);
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
  RUN_TEST(test_armed_timeout_returns_to_safe_without_latched_fault);
  RUN_TEST(test_no_drive_entry_with_throttle_pressed);
  RUN_TEST(test_wheel_loss_in_drive_causes_controlled_stop_then_latched_fault);
  RUN_TEST(test_stopping_cap_forces_fault_even_if_never_stopped);
  RUN_TEST(test_disarm_in_drive_is_clean_stop_no_fault);
  RUN_TEST(test_health_failure_during_clean_stop_upgrades_to_fault);
  RUN_TEST(test_fault_clear_requires_request_rest_and_health);
  RUN_TEST(test_no_spontaneous_clear_without_request);
  return UNITY_END();
}
