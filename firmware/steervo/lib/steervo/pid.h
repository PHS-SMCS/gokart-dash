// PID controller with output clamping and integral anti-windup
// (integration halts while the output is saturated in the same direction).
//
// The derivative acts on the MEASUREMENT, not the error, and is low-pass
// filtered. Derivative-on-measurement matters here because the setpoint is the
// steering wheel: differentiating the error would inject a huge one-tick "kick"
// every time the wheel moves, slamming the output and making the loop ring the
// instant you turn. Differentiating the measurement instead makes the D term a
// clean velocity-damping term (the Talon drives motor *speed*, so position has
// a built-in integrator that needs D to be stable), unaffected by setpoint
// steps. The filter keeps pot ADC noise from swamping D.
#pragma once

#include <stdint.h>

namespace steervo {

class Pid {
 public:
  // Derivative low-pass coefficient per call. At the 100 Hz control rate this
  // is roughly a 3–4 Hz cutoff: fast enough to damp the position loop, slow
  // enough to reject pot noise.
  static constexpr float kDerivFilterAlpha = 0.2f;

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
    prev_meas_ = 0.0f;
    d_filt_ = 0.0f;
    primed_ = false;
  }

  // error = setpoint - measurement; measurement in the same units as error
  // (centi-degrees here). Returns output in [-out_limit, out_limit].
  float update(float error, float measurement, uint32_t dt_ms) {
    float dt_s = (float)dt_ms / 1000.0f;
    if (dt_s <= 0.0f) {
      return last_out_;
    }

    // Derivative of the measurement (negated so its damping sense matches a
    // classic error-derivative for a constant setpoint). No setpoint-step kick.
    float d_raw = 0.0f;
    if (primed_) {
      d_raw = -(measurement - prev_meas_) / dt_s;
    }
    prev_meas_ = measurement;
    primed_ = true;
    d_filt_ += kDerivFilterAlpha * (d_raw - d_filt_);

    float unclamped = kp_ * error + ki_ * integral_ + kd_ * d_filt_;

    // Anti-windup: only integrate when not pushing further into saturation.
    bool saturated_high = unclamped >= out_limit_ && error > 0.0f;
    bool saturated_low = unclamped <= -out_limit_ && error < 0.0f;
    if (!saturated_high && !saturated_low) {
      integral_ += error * dt_s;
    }

    float out = kp_ * error + ki_ * integral_ + kd_ * d_filt_;
    if (out > out_limit_) out = out_limit_;
    if (out < -out_limit_) out = -out_limit_;
    last_out_ = out;
    return out;
  }

 private:
  float kp_, ki_, kd_;
  float out_limit_;
  float integral_ = 0.0f;
  float prev_meas_ = 0.0f;
  float d_filt_ = 0.0f;
  float last_out_ = 0.0f;
  bool primed_ = false;
};

}  // namespace steervo
