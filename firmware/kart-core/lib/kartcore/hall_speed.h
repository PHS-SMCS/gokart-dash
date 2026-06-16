// Hall pulse -> speed estimator (spec §3.2 / Phase T3).
//
// The Teensy ISR maintains a monotonically increasing pulse count; this module
// turns that count plus a timestamp into a pulse frequency and a "stopped"
// determination. Conversion of pulses -> mph (pulses-per-rev, wheel diameter)
// is intentionally done Pi-side (uart-protocol.md), so this module only
// exposes the raw frequency (×10 Hz) the protocol carries.
//
// `stopped()` gates DRIVE entry and controlled-stop completion, so it is
// deliberately conservative: stopped only after `stop_timeout_ms` with no new
// pulses. Pure C++ (no Arduino) for host testing.
#pragma once

#include <stdint.h>

namespace kart {

class HallSpeed {
 public:
  // window_ms: frequency is recomputed over this sliding window (smooths the
  //   1-pulse quantization at the 100 Hz tick).
  // stop_timeout_ms: no pulse edge for this long => stopped.
  HallSpeed(uint32_t window_ms = 100, uint32_t stop_timeout_ms = 300)
      : window_ms_(window_ms), stop_timeout_ms_(stop_timeout_ms) {}

  // Call every tick with the cumulative pulse count and current time. The kart
  // starts stopped: it is only "moving" once a fresh pulse edge has been seen
  // within the last stop_timeout_ms.
  void update(uint32_t count, uint32_t now_ms) {
    if (!started_) {
      started_ = true;
      window_count_ = count;
      window_start_ms_ = now_ms;
      last_count_ = count;
      return;  // no edge seen yet -> still stopped
    }

    if (count != last_count_) {
      last_change_ms_ = now_ms;
      last_count_ = count;
      moving_ = true;
    }

    uint32_t elapsed = (uint32_t)(now_ms - window_start_ms_);
    if (elapsed >= window_ms_) {
      uint32_t pulses = count - window_count_;
      // hz×10 = pulses / seconds * 10 = pulses * 10000 / elapsed_ms
      uint64_t hz_x10 = (uint64_t)pulses * 10000u / elapsed;
      hz_x10_ = (hz_x10 > 0xFFFF) ? 0xFFFF : (uint16_t)hz_x10;
      window_count_ = count;
      window_start_ms_ = now_ms;
    }

    if (moving_ && (uint32_t)(now_ms - last_change_ms_) >= stop_timeout_ms_) {
      moving_ = false;
      hz_x10_ = 0;
    }
  }

  bool stopped(uint32_t now_ms) const {
    if (!moving_) return true;
    return (uint32_t)(now_ms - last_change_ms_) >= stop_timeout_ms_;
  }

  uint16_t hz_x10() const { return hz_x10_; }

 private:
  uint32_t window_ms_;
  uint32_t stop_timeout_ms_;
  bool started_ = false;
  bool moving_ = false;
  uint32_t window_count_ = 0;
  uint32_t window_start_ms_ = 0;
  uint32_t last_count_ = 0;
  uint32_t last_change_ms_ = 0;
  uint16_t hz_x10_ = 0;
};

}  // namespace kart
