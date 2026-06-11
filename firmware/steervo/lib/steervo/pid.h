// PID controller with output clamping and integral anti-windup
// (integration halts while the output is saturated in the same direction).
#pragma once

#include <stdint.h>

namespace steervo {

class Pid {
 public:
  Pid(float kp, float ki, float kd, float out_limit)
      : kp_(kp), ki_(ki), kd_(kd), out_limit_(out_limit) {}

  void set_gains(float kp, float ki, float kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
  }
  void set_output_limit(float limit) { out_limit_ = limit; }
  float output_limit() const { return out_limit_; }

  void reset() {
    integral_ = 0.0f;
    prev_error_ = 0.0f;
    primed_ = false;
  }

  // error = setpoint - measurement. Returns output in [-out_limit, out_limit].
  float update(float error, uint32_t dt_ms) {
    float dt_s = (float)dt_ms / 1000.0f;
    if (dt_s <= 0.0f) {
      return last_out_;
    }

    float d = 0.0f;
    if (primed_) {
      d = (error - prev_error_) / dt_s;
    }
    prev_error_ = error;
    primed_ = true;

    float unclamped = kp_ * error + ki_ * integral_ + kd_ * d;

    // Anti-windup: only integrate when not pushing further into saturation.
    bool saturated_high = unclamped >= out_limit_ && error > 0.0f;
    bool saturated_low = unclamped <= -out_limit_ && error < 0.0f;
    if (!saturated_high && !saturated_low) {
      integral_ += error * dt_s;
    }

    float out = kp_ * error + ki_ * integral_ + kd_ * d;
    if (out > out_limit_) out = out_limit_;
    if (out < -out_limit_) out = -out_limit_;
    last_out_ = out;
    return out;
  }

 private:
  float kp_, ki_, kd_;
  float out_limit_;
  float integral_ = 0.0f;
  float prev_error_ = 0.0f;
  float last_out_ = 0.0f;
  bool primed_ = false;
};

}  // namespace steervo
