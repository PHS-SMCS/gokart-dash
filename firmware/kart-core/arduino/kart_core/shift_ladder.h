// Driver shift ladder (spec §3.x) — pure logic, host-tested.
//
// A single linear selector the driver moves with the wheel's shift paddles:
//
//     High   (D3)   ▲ upshift   (right paddle)
//     Med    (D2)
//     Low    (D1)
//     Park          (neutral — throttle inhibited, REV released)
//     Reverse       ▼ downshift (left paddle)  — REV asserted, ESC in Low
//
// Left paddle steps DOWN, right paddle steps UP, one rung per press. Every rung
// is freely selectable at any time — there is NO standstill gating, so the
// driver can never be trapped in a rung (they were, when "vehicle stopped" was
// derived from any hall edge and the wheels coasted on the stands). The ESC's
// own direction handling is the backstop against a hostile reverse-at-speed.
// main.cpp forces the ladder to Park whenever the kart is not armed/driving, so
// a fresh arm always starts in Park (neutral, throttle inhibited) until the
// driver selects a drive gear.
//
// Conversion of a ladder rung to the ESC's 3-mode speed selector lives in
// shift_speed_mode() so the caller can pulse the ESC gear-cycle line toward it.
#pragma once

#include <stdint.h>

namespace kart {

enum ShiftPos : int8_t {
  kShiftReverse = 0,
  kShiftPark = 1,
  kShiftLow = 2,
  kShiftMed = 3,
  kShiftHigh = 4,
};

// One paddle step. `up`/`down` are the (already edge-detected) paddle presses.
// Returns the new position, which equals `cur` only at the ends of the ladder.
// No standstill gating — every rung is reachable any time. `up` wins if both are
// asserted the same tick.
inline ShiftPos next_shift(ShiftPos cur, bool up, bool down) {
  int8_t np = cur;
  if (up && cur < kShiftHigh) {
    np = cur + 1;
  } else if (down && cur > kShiftReverse) {
    np = cur - 1;
  }
  return (ShiftPos)np;
}

// ESC speed mode (0 = Low, 1 = Med, 2 = High) for a ladder rung. Park and
// Reverse both hold the ESC in Low; forward gears map straight through.
inline uint8_t shift_speed_mode(ShiftPos p) {
  if (p == kShiftHigh) return 2;
  if (p == kShiftMed) return 1;
  return 0;  // Low, Park, Reverse
}

// True when the ladder inhibits throttle entirely (neutral).
inline bool shift_is_park(ShiftPos p) { return p == kShiftPark; }

// True when the driver has selected reverse (REV line asserted).
inline bool shift_is_reverse(ShiftPos p) { return p == kShiftReverse; }

}  // namespace kart
