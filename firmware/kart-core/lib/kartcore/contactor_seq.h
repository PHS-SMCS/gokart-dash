// Main-contactor sequencer (spec §3.5, Phase T2).
//
// HARDWARE NOTE (confirmed on this kart): precharge is an *always-on* 100 Ω
// power resistor fed from the BMS — it charges the ESC bus continuously
// whenever the pack is powered, well before the kart is ever armed. It is NOT
// driven by the Teensy. The Teensy controls only the main contactor (pin 32,
// open by default). So "precharge sequencing" collapses to: on engage, close
// the contactor and dwell a conservative settle time before declaring the bus
// ready for DRIVE.
//
// The optional bus-voltage sense path is kept for the future: if `has_bus_sense`
// is set, `ready` waits for `bus_voltage_ok` (with `settle_ms` as an upper-bound
// timeout that faults), and a mid-drive bus collapse faults too. With no sense
// line (today) `ready` is purely the settle dwell and there is no fault path.
//
// Pure C++ (no Arduino) for host testing.
#pragma once

#include <stdint.h>

namespace kart {

struct ContactorConfig {
  uint32_t settle_ms = 300;    // dwell after closing before bus is "ready"
  bool has_bus_sense = false;  // is a bus-voltage sense line wired?
};

class ContactorSequencer {
 public:
  enum class Phase : uint8_t { kOpen, kSettling, kClosed, kFault };

  ContactorSequencer() = default;
  explicit ContactorSequencer(const ContactorConfig &cfg) : cfg_(cfg) {}

  // engage: the state machine wants the bus live (ARMED/DRIVE/STOPPING).
  // bus_voltage_ok: from the sense line; ignored unless cfg_.has_bus_sense.
  void update(bool engage, uint32_t now_ms, bool bus_voltage_ok = true) {
    if (!engage) {
      phase_ = Phase::kOpen;  // dropping engage also clears a latched fault
      return;
    }

    switch (phase_) {
      case Phase::kOpen:
        phase_ = Phase::kSettling;
        closed_since_ms_ = now_ms;
        break;
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
  bool contactor_closed() const {
    return phase_ == Phase::kSettling || phase_ == Phase::kClosed;
  }
  bool ready() const { return phase_ == Phase::kClosed; }
  bool faulted() const { return phase_ == Phase::kFault; }

 private:
  ContactorConfig cfg_{};
  Phase phase_ = Phase::kOpen;
  uint32_t closed_since_ms_ = 0;
};

}  // namespace kart
