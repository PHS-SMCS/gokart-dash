// ARM chord detector (spec §3.1): a deliberate, hard-to-trigger-by-accident
// driver gesture that requests SAFE -> ARMED.
//
// The chord is: BOTH shoulder paddles held, brake pedal pressed, throttle
// released — sustained continuously for `hold_ms`. Releasing any condition
// before the hold completes restarts the timer. The detector emits a single
// one-shot `confirmed()` pulse when the hold completes, then latches until the
// chord is fully released, so holding the chord can never re-arm on its own
// after a disarm.
//
// Pure C++ (no Arduino) so it is unit-tested on the host.
#pragma once

#include <stdint.h>

namespace kart {

struct ArmChordInputs {
  bool paddle_left;       // LB (button 4)
  bool paddle_right;      // RB (button 5)
  bool brake_pressed;     // brake pedal past threshold
  bool throttle_released; // throttle pedal at ~0
};

class ArmChord {
 public:
  explicit ArmChord(uint32_t hold_ms = 1000) : hold_ms_(hold_ms) {}

  // Advance the detector. Returns true for exactly one tick when the chord
  // hold completes. `now_ms` must be monotonic.
  bool update(const ArmChordInputs &in, uint32_t now_ms) {
    bool held = in.paddle_left && in.paddle_right && in.brake_pressed &&
                in.throttle_released;

    if (!held) {
      forming_ = false;
      fired_ = false;  // chord fully released -> re-arm allowed
      return false;
    }

    if (fired_) {
      return false;  // still holding after a completed chord; wait for release
    }

    if (!forming_) {
      forming_ = true;
      forming_since_ms_ = now_ms;
      return false;
    }

    if ((uint32_t)(now_ms - forming_since_ms_) >= hold_ms_) {
      fired_ = true;
      forming_ = false;
      return true;
    }
    return false;
  }

  // Fraction of the hold completed (0..1), for UI/LED feedback while forming.
  float progress(uint32_t now_ms) const {
    if (!forming_) return fired_ ? 1.0f : 0.0f;
    float p = (float)(uint32_t)(now_ms - forming_since_ms_) / (float)hold_ms_;
    return p > 1.0f ? 1.0f : p;
  }

  bool forming() const { return forming_; }

 private:
  uint32_t hold_ms_;
  bool forming_ = false;
  bool fired_ = false;
  uint32_t forming_since_ms_ = 0;
};

}  // namespace kart
