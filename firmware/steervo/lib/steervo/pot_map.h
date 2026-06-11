// Steering potentiometer: plausibility window and calibrated raw -> angle
// mapping. The pot is powered from 3.3 V with its wiper on an ESP32 ADC pin
// (12-bit, 0..4095).
#pragma once

#include <stdint.h>

namespace steervo {

// Raw ADC readings outside this window indicate a broken wire, short, or
// wiper off the track: hard fault. The pot never legitimately reaches the
// rails because the steering range is a fraction of the pot's travel.
constexpr uint16_t kPotValidMin = 64;
constexpr uint16_t kPotValidMax = 4031;

inline bool pot_plausible(uint16_t raw) {
  return raw >= kPotValidMin && raw <= kPotValidMax;
}

// Calibration captured by the guided STEER_CAL sequence and persisted in NVS.
// left/right are the soft end stops; angles follow the kart convention
// (+ = right of center, centi-degrees).
struct PotCalibration {
  uint16_t raw_center;
  uint16_t raw_left;
  uint16_t raw_right;
  int16_t angle_left_cdeg;   // logical angle at raw_left (negative)
  int16_t angle_right_cdeg;  // logical angle at raw_right (positive)
  bool valid;
};

// Piecewise-linear raw -> angle around the center mark. Works for either pot
// orientation (raw_left may be greater or smaller than raw_right).
inline int16_t pot_to_angle_cdeg(const PotCalibration &cal, uint16_t raw) {
  int32_t r = raw;
  int32_t side_span;
  int32_t angle_span;
  if ((cal.raw_left < cal.raw_center) == (r < cal.raw_center)) {
    side_span = (int32_t)cal.raw_left - cal.raw_center;
    angle_span = cal.angle_left_cdeg;
  } else {
    side_span = (int32_t)cal.raw_right - cal.raw_center;
    angle_span = cal.angle_right_cdeg;
  }
  if (side_span == 0) {
    return 0;
  }
  int32_t angle = (r - (int32_t)cal.raw_center) * angle_span / side_span;
  // Clamp to the calibrated extremes.
  int32_t lo = cal.angle_left_cdeg;
  int32_t hi = cal.angle_right_cdeg;
  if (angle < lo) angle = lo;
  if (angle > hi) angle = hi;
  return (int16_t)angle;
}

}  // namespace steervo
