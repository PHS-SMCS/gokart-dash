// Tests for the driver shift ladder (Reverse < Park < Low < Med < High).
#include <unity.h>

#include "shift_ladder.h"

using kart::kShiftHigh;
using kart::kShiftLow;
using kart::kShiftMed;
using kart::kShiftPark;
using kart::kShiftReverse;
using kart::next_shift;
using kart::shift_speed_mode;

void setUp() {}
void tearDown() {}

// -------------------- one rung per paddle press ------------------------------

void test_upshift_low_to_med() {
  TEST_ASSERT_EQUAL(kShiftMed, next_shift(kShiftLow, /*up=*/true, false));
}

void test_upshift_med_to_high() {
  TEST_ASSERT_EQUAL(kShiftHigh, next_shift(kShiftMed, true, false));
}

void test_downshift_high_to_med() {
  TEST_ASSERT_EQUAL(kShiftMed, next_shift(kShiftHigh, false, /*down=*/true));
}

void test_downshift_med_to_low() {
  TEST_ASSERT_EQUAL(kShiftLow, next_shift(kShiftMed, false, true));
}

// The whole point of the fix: Low<->Park and Park<->Reverse are freely
// reachable, with NO standstill gating (the caller passes no motion state).

void test_downshift_low_to_park() {
  TEST_ASSERT_EQUAL(kShiftPark, next_shift(kShiftLow, false, true));
}

void test_upshift_park_to_low() {
  TEST_ASSERT_EQUAL(kShiftLow, next_shift(kShiftPark, true, false));
}

void test_downshift_park_to_reverse() {
  // The headline behavior: down paddle again while in Park -> Reverse, any time.
  TEST_ASSERT_EQUAL(kShiftReverse, next_shift(kShiftPark, false, true));
}

void test_upshift_reverse_to_park() {
  TEST_ASSERT_EQUAL(kShiftPark, next_shift(kShiftReverse, true, false));
}

// -------------------- ladder ends clamp --------------------------------------

void test_upshift_clamps_at_high() {
  TEST_ASSERT_EQUAL(kShiftHigh, next_shift(kShiftHigh, true, false));
}

void test_downshift_clamps_at_reverse() {
  TEST_ASSERT_EQUAL(kShiftReverse, next_shift(kShiftReverse, false, true));
}

// -------------------- no paddle / both paddles -------------------------------

void test_no_paddle_holds_position() {
  TEST_ASSERT_EQUAL(kShiftLow, next_shift(kShiftLow, false, false));
}

void test_both_paddles_upshift_wins() {
  TEST_ASSERT_EQUAL(kShiftMed, next_shift(kShiftLow, true, true));
}

// -------------------- ladder -> ESC speed-mode mapping -----------------------

void test_speed_mode_park_and_reverse_hold_low() {
  TEST_ASSERT_EQUAL_UINT8(0, shift_speed_mode(kShiftReverse));
  TEST_ASSERT_EQUAL_UINT8(0, shift_speed_mode(kShiftPark));
  TEST_ASSERT_EQUAL_UINT8(0, shift_speed_mode(kShiftLow));
}

void test_speed_mode_forward_gears() {
  TEST_ASSERT_EQUAL_UINT8(1, shift_speed_mode(kShiftMed));
  TEST_ASSERT_EQUAL_UINT8(2, shift_speed_mode(kShiftHigh));
}

// Adjacent rungs never step the ESC mode by more than one — so a single paddle
// press is always reachable with one gear-cycle pulse sequence.
void test_adjacent_rungs_step_esc_mode_by_at_most_one() {
  const kart::ShiftPos ladder[] = {kShiftReverse, kShiftPark, kShiftLow,
                                   kShiftMed, kShiftHigh};
  for (int i = 0; i + 1 < 5; ++i) {
    int d = (int)shift_speed_mode(ladder[i + 1]) - (int)shift_speed_mode(ladder[i]);
    if (d < 0) d = -d;
    TEST_ASSERT_TRUE(d <= 1);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_upshift_low_to_med);
  RUN_TEST(test_upshift_med_to_high);
  RUN_TEST(test_downshift_high_to_med);
  RUN_TEST(test_downshift_med_to_low);
  RUN_TEST(test_downshift_low_to_park);
  RUN_TEST(test_upshift_park_to_low);
  RUN_TEST(test_downshift_park_to_reverse);
  RUN_TEST(test_upshift_reverse_to_park);
  RUN_TEST(test_upshift_clamps_at_high);
  RUN_TEST(test_downshift_clamps_at_reverse);
  RUN_TEST(test_no_paddle_holds_position);
  RUN_TEST(test_both_paddles_upshift_wins);
  RUN_TEST(test_speed_mode_park_and_reverse_hold_low);
  RUN_TEST(test_speed_mode_forward_gears);
  RUN_TEST(test_adjacent_rungs_step_esc_mode_by_at_most_one);
  return UNITY_END();
}
