// Main-contactor sequencer with software precharge (spec §3.5, Phase T2).
//
// HARDWARE (rev. July 2026): precharge is a Teensy-controlled resistor on
// `kPinPrecharge` (GPIO27) — HIGH = resistor energized, LOW = off. The main
// contactor is on GPIO32 (open by default). Earlier hardware had an always-on
// precharge resistor; that is gone, and closing the contactor into an
// uncharged bus is what welded the previous contactor shut.
//
// So engaging the bus is now strictly:
//
//   OPEN -> PRECHARGE (resistor on, contactor open, `precharge_ms`)
//        -> SETTLING  (resistor OFF, contactor closed, `settle_ms`)
//        -> CLOSED    (bus ready for DRIVE)
//
// SAFETY — the precharge resistor melts its enclosure if left energized. Two
// independent limits protect it, both enforced here:
//   * `precharge_max_ms` — a hard cap. If the resistor has been on longer than
//     this (a stalled tick, a clock anomaly, a wedged caller), the sequencer
//     drops precharge, leaves the contactor open, and latches kFault.
//   * `precharge_cooldown_ms` — a minimum off-time between precharge cycles, so
//     repeated arm/disarm cycling cannot duty-cycle the resistor hot. A re-
//     engage inside the cooldown waits in kCooldown (bus not ready) rather than
//     skipping precharge, which would be the unsafe shortcut.
// `precharge_on()` is only ever true in kPrecharge; every other phase — kFault
// included — reports it off. Callers must additionally fail the pin LOW at
// boot and on any watchdog/fault path.
//
// The optional bus-voltage sense path is kept for the future: if `has_bus_sense`
// is set, `ready` waits for `bus_voltage_ok` (with `settle_ms` as an upper-bound
// timeout that faults), and a mid-drive bus collapse faults too. With no sense
// line (today) `ready` is purely the settle dwell.
//
// Pure C++ (no Arduino) for host testing.
#pragma once

#include <stdint.h>

namespace kart {

struct ContactorConfig {
  uint32_t precharge_ms = 2000;           // resistor on before the contactor closes
  uint32_t precharge_max_ms = 3000;       // hard cap on resistor on-time -> fault
  uint32_t precharge_cooldown_ms = 10000; // minimum resistor off-time between cycles
  uint32_t settle_ms = 500;               // dwell after closing before bus is "ready"
  bool has_bus_sense = false;             // is a bus-voltage sense line wired?
};

class ContactorSequencer {
 public:
  enum class Phase : uint8_t {
    kOpen,
    kCooldown,
    kPrecharge,
    kSettling,
    kClosed,
    kFault
  };

  ContactorSequencer() = default;
  explicit ContactorSequencer(const ContactorConfig &cfg) : cfg_(cfg) {}

  // engage: the state machine wants the bus live (ARMED/DRIVE/STOPPING).
  // bus_voltage_ok: from the sense line; ignored unless cfg_.has_bus_sense.
  void update(bool engage, uint32_t now_ms, bool bus_voltage_ok = true) {
    if (!engage) {
      // Dropping engage opens everything and clears a latched fault. If we were
      // mid-precharge, remember when the resistor went off so the cooldown
      // still applies to the next attempt.
      if (phase_ == Phase::kPrecharge) endPrecharge(now_ms);
      phase_ = Phase::kOpen;
      return;
    }

    switch (phase_) {
      case Phase::kOpen:
      case Phase::kCooldown:
        if (prechargeAllowed(now_ms)) {
          phase_ = Phase::kPrecharge;
          precharge_since_ms_ = now_ms;
        } else {
          phase_ = Phase::kCooldown;  // resistor still cooling; bus stays open
        }
        break;

      case Phase::kPrecharge: {
        uint32_t on_ms = (uint32_t)(now_ms - precharge_since_ms_);
        if (on_ms >= cfg_.precharge_max_ms) {
          // Overrun: the resistor has been on too long. Shed it and fault
          // rather than close the contactor on an unknown bus state.
          endPrecharge(now_ms);
          phase_ = Phase::kFault;
        } else if (on_ms >= cfg_.precharge_ms) {
          endPrecharge(now_ms);
          phase_ = Phase::kSettling;
          closed_since_ms_ = now_ms;
        }
        break;
      }

      case Phase::kSettling:
        if (cfg_.has_bus_sense && bus_voltage_ok) {
          phase_ = Phase::kClosed;
        } else if ((uint32_t)(now_ms - closed_since_ms_) >= cfg_.settle_ms) {
          phase_ = (cfg_.has_bus_sense && !bus_voltage_ok) ? Phase::kFault
                                                           : Phase::kClosed;
        }
        break;

      case Phase::kClosed:
        if (cfg_.has_bus_sense && !bus_voltage_ok) {
          phase_ = Phase::kFault;  // bus collapsed under load
        }
        break;

      case Phase::kFault:
        break;  // latched until engage drops
    }
  }

  Phase phase() const { return phase_; }
  const char *phase_name() const {
    switch (phase_) {
      case Phase::kOpen: return "open";
      case Phase::kCooldown: return "cooldown";
      case Phase::kPrecharge: return "precharge";
      case Phase::kSettling: return "settling";
      case Phase::kClosed: return "closed";
      case Phase::kFault: return "fault";
    }
    return "?";
  }

  // The precharge resistor is energized in exactly one phase, and never while
  // the contactor is closed.
  bool precharge_on() const { return phase_ == Phase::kPrecharge; }
  bool contactor_closed() const {
    return phase_ == Phase::kSettling || phase_ == Phase::kClosed;
  }
  bool ready() const { return phase_ == Phase::kClosed; }
  bool faulted() const { return phase_ == Phase::kFault; }

  // How long the resistor has been energized (0 when it is off) — for the
  // caller's independent hardware watchdog and for STATUS.
  uint32_t precharge_elapsed_ms(uint32_t now_ms) const {
    return precharge_on() ? (uint32_t)(now_ms - precharge_since_ms_) : 0;
  }

 private:
  bool prechargeAllowed(uint32_t now_ms) const {
    if (!have_precharged_) return true;  // first cycle since boot
    return (uint32_t)(now_ms - precharge_off_ms_) >= cfg_.precharge_cooldown_ms;
  }
  void endPrecharge(uint32_t now_ms) {
    have_precharged_ = true;
    precharge_off_ms_ = now_ms;
  }

  ContactorConfig cfg_{};
  Phase phase_ = Phase::kOpen;
  uint32_t precharge_since_ms_ = 0;
  uint32_t precharge_off_ms_ = 0;
  bool have_precharged_ = false;
  uint32_t closed_since_ms_ = 0;
};

}  // namespace kart
