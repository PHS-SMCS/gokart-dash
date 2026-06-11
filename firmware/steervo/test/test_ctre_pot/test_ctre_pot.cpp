// Tests for CTRE frame construction and pot mapping.
#include <unity.h>

#include "ctre_frames.h"
#include "pot_map.h"

void setUp() {}
void tearDown() {}

// -------------------- CTRE frames --------------------

void test_percent_output_frame_id_and_demand() {
  // Device 0, full forward: ID = base | control | dev, demand = +1023.
  steervo::CanFrame f = steervo::talon_percent_output(0, 1.0f);
  TEST_ASSERT_EQUAL_HEX32(0x02040080u, f.id);
  TEST_ASSERT_EQUAL(8, f.dlc);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, f.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.data[2]);  // 1023 = 0x0003FF
}

void test_percent_output_negative_demand_sign_extends() {
  // -1.0 -> -1023 as signed 24-bit = 0xFFFC01.
  steervo::CanFrame f = steervo::talon_percent_output(3, -1.0f);
  TEST_ASSERT_EQUAL_HEX32(0x02040083u, f.id);
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFC, f.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x01, f.data[2]);
}

void test_percent_output_clamps_and_zero() {
  steervo::CanFrame f = steervo::talon_percent_output(0, 2.5f);
  TEST_ASSERT_EQUAL_HEX8(0x03, f.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.data[2]);

  f = steervo::talon_percent_output(0, 0.0f);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.data[2]);
}

void test_device_number_masked_to_6_bits() {
  steervo::CanFrame f = steervo::talon_percent_output(0x7F, 0.0f);
  TEST_ASSERT_EQUAL_HEX32(0x020400BFu, f.id);  // 0x7F & 0x3F = 0x3F
}

void test_global_enable_frame() {
  steervo::CanFrame f = steervo::ctre_global_enable(true);
  TEST_ASSERT_EQUAL_HEX32(0x000401BFu, f.id);
  TEST_ASSERT_EQUAL(8, f.dlc);
  TEST_ASSERT_EQUAL(1, f.data[0]);

  f = steervo::ctre_global_enable(false);
  TEST_ASSERT_EQUAL(0, f.data[0]);
}

// -------------------- pot mapping --------------------

void test_pot_plausibility_window() {
  TEST_ASSERT_FALSE(steervo::pot_plausible(0));     // shorted low
  TEST_ASSERT_FALSE(steervo::pot_plausible(4095));  // shorted high / broken
  TEST_ASSERT_TRUE(steervo::pot_plausible(64));
  TEST_ASSERT_TRUE(steervo::pot_plausible(2048));
  TEST_ASSERT_TRUE(steervo::pot_plausible(4031));
}

steervo::PotCalibration sym_cal() {
  steervo::PotCalibration cal{};
  cal.raw_center = 2048;
  cal.raw_left = 1024;
  cal.raw_right = 3072;
  cal.angle_left_cdeg = -3000;
  cal.angle_right_cdeg = 3000;
  cal.valid = true;
  return cal;
}

void test_pot_to_angle_center_and_marks() {
  steervo::PotCalibration cal = sym_cal();
  TEST_ASSERT_EQUAL(0, steervo::pot_to_angle_cdeg(cal, 2048));
  TEST_ASSERT_EQUAL(-3000, steervo::pot_to_angle_cdeg(cal, 1024));
  TEST_ASSERT_EQUAL(3000, steervo::pot_to_angle_cdeg(cal, 3072));
  TEST_ASSERT_EQUAL(1500, steervo::pot_to_angle_cdeg(cal, 2560));
}

void test_pot_to_angle_clamps_beyond_marks() {
  steervo::PotCalibration cal = sym_cal();
  TEST_ASSERT_EQUAL(-3000, steervo::pot_to_angle_cdeg(cal, 500));
  TEST_ASSERT_EQUAL(3000, steervo::pot_to_angle_cdeg(cal, 3800));
}

void test_pot_to_angle_asymmetric_calibration() {
  // Off-center mechanical mounting: unequal spans per side.
  steervo::PotCalibration cal{};
  cal.raw_center = 1800;
  cal.raw_left = 1200;
  cal.raw_right = 3000;
  cal.angle_left_cdeg = -3000;
  cal.angle_right_cdeg = 3000;
  cal.valid = true;
  TEST_ASSERT_EQUAL(0, steervo::pot_to_angle_cdeg(cal, 1800));
  TEST_ASSERT_EQUAL(-1500, steervo::pot_to_angle_cdeg(cal, 1500));
  TEST_ASSERT_EQUAL(1500, steervo::pot_to_angle_cdeg(cal, 2400));
}

void test_pot_to_angle_reversed_pot_orientation() {
  // Pot wired so left = high raw values.
  steervo::PotCalibration cal{};
  cal.raw_center = 2048;
  cal.raw_left = 3072;
  cal.raw_right = 1024;
  cal.angle_left_cdeg = -3000;
  cal.angle_right_cdeg = 3000;
  cal.valid = true;
  TEST_ASSERT_EQUAL(-3000, steervo::pot_to_angle_cdeg(cal, 3072));
  TEST_ASSERT_EQUAL(3000, steervo::pot_to_angle_cdeg(cal, 1024));
  TEST_ASSERT_EQUAL(-1500, steervo::pot_to_angle_cdeg(cal, 2560));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_percent_output_frame_id_and_demand);
  RUN_TEST(test_percent_output_negative_demand_sign_extends);
  RUN_TEST(test_percent_output_clamps_and_zero);
  RUN_TEST(test_device_number_masked_to_6_bits);
  RUN_TEST(test_global_enable_frame);
  RUN_TEST(test_pot_plausibility_window);
  RUN_TEST(test_pot_to_angle_center_and_marks);
  RUN_TEST(test_pot_to_angle_clamps_beyond_marks);
  RUN_TEST(test_pot_to_angle_asymmetric_calibration);
  RUN_TEST(test_pot_to_angle_reversed_pot_orientation);
  return UNITY_END();
}
