// Maps a raw pedal/wheel axis (Hori reports signed 16-bit) to a calibrated
// 0..100 % command with a low-end deadband, plus a range plausibility check.
#pragma once

#include <stdint.h>

namespace kart {

struct PedalCal {
  int32_t raw_released;   // axis value with the pedal untouched
  int32_t raw_pressed;    // axis value at full travel
  float deadband_pct;     // output below this maps to 0 (default 3 %)
  int32_t margin;         // plausibility margin beyond the calibrated range
};

// Hori Racing Wheel Overdrive pedals: released = -32767, pressed = +32767.
constexpr PedalCal kHoriPedalCal{-32767, 32767, 3.0f, 2048};

class PedalMap {
 public:
  explicit PedalMap(const PedalCal &cal) : cal_(cal) {}

  // Raw axis -> 0..100. Values outside the calibrated range clamp.
  float map(int32_t raw) const {
    float span = (float)(cal_.raw_pressed - cal_.raw_released);
    float norm = (float)(raw - cal_.raw_released) / span;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    float pct = norm * 100.0f;
    return (pct < cal_.deadband_pct) ? 0.0f : pct;
  }

  // False when the raw value is outside the calibrated range by more than
  // the margin — e.g. a wiring fault or a wildly different device. The
  // caller debounces this (>250 ms) before raising PEDAL_IMPLAUSIBLE.
  bool plausible(int32_t raw) const {
    int32_t lo = (cal_.raw_released < cal_.raw_pressed) ? cal_.raw_released
                                                        : cal_.raw_pressed;
    int32_t hi = (cal_.raw_released < cal_.raw_pressed) ? cal_.raw_pressed
                                                        : cal_.raw_released;
    return raw >= lo - cal_.margin && raw <= hi + cal_.margin;
  }

 private:
  PedalCal cal_;
};

}  // namespace kart
