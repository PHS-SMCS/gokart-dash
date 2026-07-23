// Host-side tests for the Teensy steering-link helper.
#include <unity.h>

#include "steer_link.h"

using kart::SteerLink;
using kart::SteerLinkConfig;
using kart::SteerState;
using kart::SteerStatus;

void setUp() {}
void tearDown() {}

// -------------------- axis -> setpoint mapping --------------------

void test_axis_maps_full_scale_to_range() {
  SteerLink link;  // defaults: ±32767 -> ±3000 cdeg
  TEST_ASSERT_EQUAL(kart::kSteerRangeCdeg, link.axis_to_setpoint(32767));
  TEST_ASSERT_EQUAL((int)-kart::kSteerRangeCdeg, link.axis_to_setpoint(-32768));
}

void test_axis_center_deadband_snaps_to_zero() {
  SteerLink link;  // ±0.30° deadband
  TEST_ASSERT_EQUAL(0, link.axis_to_setpoint(0));
  TEST_ASSERT_EQUAL(0, link.axis_to_setpoint(100));   // tiny jitter -> 0
  TEST_ASSERT_EQUAL(0, link.axis_to_setpoint(-100));
}

void test_axis_is_proportional_and_signed() {
  SteerLink link;
  // Half right deflection -> about half range, positive.
  int16_t half = link.axis_to_setpoint(16384);
  TEST_ASSERT_TRUE(half > 1400 && half < 1600);
  TEST_ASSERT_TRUE(link.axis_to_setpoint(-16384) < 0);
}

void test_axis_invert_flips_command_sense() {
  kart::SteerLinkConfig cfg;
  cfg.invert = true;
  SteerLink link(cfg);
  // Same magnitude as the non-inverted mapping, opposite sign.
  TEST_ASSERT_EQUAL((int)-kart::kSteerRangeCdeg, link.axis_to_setpoint(32767));
  TEST_ASSERT_EQUAL(kart::kSteerRangeCdeg, link.axis_to_setpoint(-32768));
  TEST_ASSERT_TRUE(link.axis_to_setpoint(16384) < 0);   // right wheel -> negative
  TEST_ASSERT_EQUAL(0, link.axis_to_setpoint(0));       // center still center
}

void test_axis_clamps_beyond_full_scale() {
  SteerLink link;
  TEST_ASSERT_EQUAL(kart::kSteerRangeCdeg, link.axis_to_setpoint(100000));
  TEST_ASSERT_EQUAL((int)-kart::kSteerRangeCdeg, link.axis_to_setpoint(-100000));
}

// -------------------- heartbeat / health --------------------

SteerStatus status(SteerState st, uint8_t fault_bits = 0) {
  SteerStatus s{};
  s.state = st;
  s.fault_bits = fault_bits;
  return s;
}

void test_link_not_ok_before_any_status() {
  SteerLink link;
  TEST_ASSERT_FALSE(link.link_ok(1000));
  TEST_ASSERT_FALSE(link.calibrated());
  TEST_ASSERT_FALSE(link.reports_fault());
}

void test_link_ok_while_fresh_then_stale() {
  SteerLink link;
  link.on_status(status(SteerState::kReady), 1000);
  TEST_ASSERT_TRUE(link.link_ok(1000));
  TEST_ASSERT_TRUE(link.link_ok(1000 + kart::kSteerStatusTimeoutMs - 1));
  // At/after the timeout the heartbeat is stale.
  TEST_ASSERT_FALSE(link.link_ok(1000 + kart::kSteerStatusTimeoutMs));
}

void test_calibrated_reflects_not_calibrated_bit() {
  SteerLink link;
  link.on_status(status(SteerState::kReady, kart::kSteerFaultNotCalibrated), 10);
  TEST_ASSERT_FALSE(link.calibrated());
  link.on_status(status(SteerState::kReady, 0), 20);
  TEST_ASSERT_TRUE(link.calibrated());
}

void test_reports_fault_only_in_fault_state() {
  SteerLink link;
  link.on_status(status(SteerState::kActive), 10);
  TEST_ASSERT_FALSE(link.reports_fault());
  link.on_status(status(SteerState::kFault, kart::kSteerFaultStall), 20);
  TEST_ASSERT_TRUE(link.reports_fault());
}

void test_seq_counter_increments_and_wraps() {
  SteerLink link;
  TEST_ASSERT_EQUAL(0, link.next_seq());
  TEST_ASSERT_EQUAL(1, link.next_seq());
  for (int i = 2; i < 256; i++) link.next_seq();
  TEST_ASSERT_EQUAL(0, link.next_seq());  // wrapped
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_axis_maps_full_scale_to_range);
  RUN_TEST(test_axis_center_deadband_snaps_to_zero);
  RUN_TEST(test_axis_is_proportional_and_signed);
  RUN_TEST(test_axis_invert_flips_command_sense);
  RUN_TEST(test_axis_clamps_beyond_full_scale);
  RUN_TEST(test_link_not_ok_before_any_status);
  RUN_TEST(test_link_ok_while_fresh_then_stale);
  RUN_TEST(test_calibrated_reflects_not_calibrated_bit);
  RUN_TEST(test_reports_fault_only_in_fault_state);
  RUN_TEST(test_seq_counter_increments_and_wraps);
  return UNITY_END();
}
