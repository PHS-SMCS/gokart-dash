// Asymmetric slew-rate limiter for the throttle path: rising commands ramp
// at a configured rate; falling commands are immediate by default so lifting
// the pedal always cuts power instantly. A non-positive rise rate disables the
// ramp entirely (rising commands pass through instantly, like the fall).
#pragma once

#include <stdint.h>

namespace kart {

class SlewLimiter {
 public:
  // Rates in units per second. A non-positive rate (rise or fall) means
  // "unlimited" (instant change in that direction).
  SlewLimiter(float rise_per_s, float fall_per_s = 0.0f)
      : rise_per_s_(rise_per_s), fall_per_s_(fall_per_s) {}

  float update(float target, uint32_t dt_ms) {
    float dt_s = (float)dt_ms / 1000.0f;
    if (target > value_) {
      if (rise_per_s_ <= 0.0f) {
        value_ = target;
      } else {
        float max_step = rise_per_s_ * dt_s;
        float step = target - value_;
        value_ += (step > max_step) ? max_step : step;
      }
    } else if (target < value_) {
      if (fall_per_s_ <= 0.0f) {
        value_ = target;
      } else {
        float max_step = fall_per_s_ * dt_s;
        float step = value_ - target;
        value_ -= (step > max_step) ? max_step : step;
      }
    }
    return value_;
  }

  void reset(float value = 0.0f) { value_ = value; }
  float value() const { return value_; }

 private:
  float rise_per_s_;
  float fall_per_s_;
  float value_ = 0.0f;
};

}  // namespace kart
