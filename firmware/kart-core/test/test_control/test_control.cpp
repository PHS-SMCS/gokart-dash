// Tests for the throttle slew limiter and pedal mapping.
#include <unity.h>

#include "pedal_map.h"
#include "slew_limiter.h"

void setUp() {}
void tearDown() {}

// -------------------- SlewLimiter --------------------

void test_slew_rise_is_rate_limited() {
  kart::SlewLimiter s(/*rise_per_s=*/50.0f);
  // 100 ms at 50 %/s -> max +5 per step.
  TEST_ASSERT_EQUAL_FLOAT(5.0f, s.update(100.0f, 100));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, s.update(100.0f, 100));
}

void test_slew_fall_is_instant_by_default() {
  kart::SlewLimiter s(/*rise_per_s=*/50.0f);
  s.reset(80.0f);
  // Lifting the pedal must cut power immediately.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.update(0.0f, 10));
}

void test_slew_does_not_overshoot_target() {
  kart::SlewLimiter s(/*rise_per_s=*/50.0f);
  s.reset(8.0f);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, s.update(10.0f, 100));  // step capped at +5
  TEST_ASSERT_EQUAL_FLOAT(10.0f, s.update(10.0f, 100));  // holds at target
}

void test_slew_limited_fall_when_configured() {
  kart::SlewLimiter s(/*rise_per_s=*/50.0f, /*fall_per_s=*/20.0f);
  s.reset(10.0f);
  TEST_ASSERT_EQUAL_FLOAT(8.0f, s.update(0.0f, 100));
}

// -------------------- PedalMap --------------------

void test_pedal_released_maps_to_zero() {
  kart::PedalMap p(kart::kHoriPedalCal);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.map(-32767));
}

void test_pedal_pressed_maps_to_full() {
  kart::PedalMap p(kart::kHoriPedalCal);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, p.map(32767));
}

void test_pedal_midpoint_maps_near_half() {
  kart::PedalMap p(kart::kHoriPedalCal);
  float mid = p.map(0);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, mid);
}

void test_pedal_deadband_suppresses_noise_near_released() {
  kart::PedalMap p(kart::kHoriPedalCal);
  // ~1 % travel: inside the 3 % deadband -> exactly 0.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.map(-32767 + 655));
}

void test_pedal_out_of_range_clamps() {
  // A narrower calibration (realistic once the usable range is measured).
  kart::PedalCal cal{-30000, 30000, 3.0f, 2048};
  kart::PedalMap p(cal);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, p.map(32000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.map(-32000));
}

void test_pedal_plausibility_with_margin() {
  kart::PedalCal cal{-30000, 30000, 3.0f, 1000};
  kart::PedalMap p(cal);
  TEST_ASSERT_TRUE(p.plausible(-30500));   // within margin
  TEST_ASSERT_TRUE(p.plausible(0));
  TEST_ASSERT_FALSE(p.plausible(-32000));  // beyond margin -> implausible
  TEST_ASSERT_FALSE(p.plausible(32000));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_slew_rise_is_rate_limited);
  RUN_TEST(test_slew_fall_is_instant_by_default);
  RUN_TEST(test_slew_does_not_overshoot_target);
  RUN_TEST(test_slew_limited_fall_when_configured);
  RUN_TEST(test_pedal_released_maps_to_zero);
  RUN_TEST(test_pedal_pressed_maps_to_full);
  RUN_TEST(test_pedal_midpoint_maps_near_half);
  RUN_TEST(test_pedal_deadband_suppresses_noise_near_released);
  RUN_TEST(test_pedal_out_of_range_clamps);
  RUN_TEST(test_pedal_plausibility_with_margin);
  return UNITY_END();
}
