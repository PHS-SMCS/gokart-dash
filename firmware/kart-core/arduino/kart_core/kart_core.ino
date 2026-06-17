// kart-core — Teensy 4.1 motion-authority firmware.
//
// Traction track (SOFTWARE-STACK-PLAN.md §3, Phases T1–T4):
//   T1  Hori RT pedal -> deadband -> curve -> slew limiter -> MCP4725 DAC.
//   T2  ESC discrete lines (brake-low, reverse, speed select) + main-contactor
//       sequencing (external always-on precharge; Teensy closes pin 32).
//   T3  Hall pulse capture -> speed; zero-speed gates DRIVE entry.
//   T4  Traction-only bench mode (§3.6) wired; spinning the motor is a
//       supervised, on-stands step that needs the ESC powered + Ben present.
//
// Steering: the Hori wheel axis -> STEER_SET on CAN3 (1 Mbps) at 50 Hz to the
// Steervo, which runs the position loop and drives the Talon over PWM; its
// STEER_STATUS heartbeat feeds steering health back here. The motor only moves
// when steering is explicitly enabled at runtime (`STEER ON`, default off) and
// the link is healthy + calibrated. Traction stays bench-gated separately via
// KART_TRACTION_ONLY_BENCH (config.h). The Pi UART carries the human-readable
// command channel plus the 20 Hz binary telemetry stream (uart-protocol.md).
//
// Safety: boots into SAFE with all outputs deterministically off. The state
// machine (lib/kartcore/drive_state) is the single source of motion authority;
// the throttle DAC is only driven in DRIVE. The Pi cannot command motion.

#include <Arduino.h>
#include <FlexCAN_T4.h>
#include <Wire.h>

#include "config.h"  // defines KART_* flags used by the includes below

#include <USBHost_t36.h>

#if KART_ENABLE_WATCHDOG
#include "Watchdog_t4.h"
#endif

#include "arm_chord.h"
#include "contactor_seq.h"
#include "drive_state.h"
#include "hall_speed.h"
#include "kart_can.h"
#include "pedal_map.h"
#include "slew_limiter.h"
#include "steer_link.h"
#include "telemetry.h"

namespace cfg = kart::cfg;

namespace {

constexpr const char *kVersion = "0.4.0-steering";

// ── Pin map (docs/SMCSKart-Mainboard/README.md). Output lines are
// MOSFET-switched grounds: HIGH = asserted at the ESC, LOW = released. ──
constexpr uint8_t kPinHallPulses = 2;
constexpr uint8_t kPinReverse = 3;
constexpr uint8_t kPinBrakeLow = 4;
constexpr uint8_t kPinSpeedHigh = 5;
constexpr uint8_t kPinSpeedLow = 6;
constexpr uint8_t kPinCruise = 9;
constexpr uint8_t kPinPps = 10;
constexpr uint8_t kPinContactor = 32;
constexpr uint8_t kPinLedBlue = 33;
constexpr uint8_t kPinLedGreen = 36;
constexpr uint8_t kPinLedRed = 37;

constexpr uint8_t kMcp4725Addr = 0x60;
constexpr uint16_t kDacMax = 4095;

// ── Core objects ──
kart::DriveStateMachine g_dsm;
kart::SlewLimiter g_throttleSlew(cfg::kThrottleSlewRisePerS,
                                 cfg::kThrottleSlewFallPerS);
kart::ArmChord g_armChord(cfg::kArmChordHoldMs);
kart::HallSpeed g_hall(cfg::kHallWindowMs, cfg::kHallStopTimeoutMs);
kart::ContactorSequencer g_contactor(
    kart::ContactorConfig{cfg::kContactorSettleMs, /*has_bus_sense=*/false});

// ── Steering CAN link (CAN3 = mainboard pins 30/31 via MCP2562) ──
FlexCAN_T4<CAN3, RX_SIZE_64, TX_SIZE_16> g_can;
kart::SteerLink g_steerLink;
// Runtime steering-output gate. SAFETY: default OFF — the steering motor never
// moves until an explicit `STEER ON` (the supervised go-ahead), independent of
// the traction drive state. STEER_SET frames stream regardless (enable=0 keeps
// the Steervo in READY); only the ENABLE bit is gated.
bool g_steerEnabled = false;
int16_t g_steerSetpointCdeg = 0;
uint32_t g_lastSteerSetMs = 0;

// ── USB host (mainboard USB-A -> Teensy host header) ──
USBHost g_usbHost;
USBHub g_usbHub1(g_usbHost);
USBHub g_usbHub2(g_usbHost);
JoystickController g_joystick(g_usbHost);

// ── Bench-tunable wheel/pedal config (CFG command; RAM only, lost on reboot) ──
int g_axisThrottle = cfg::kAxisThrottle;
int g_axisBrake = cfg::kAxisBrake;
int g_axisSteer = cfg::kAxisSteer;
kart::PedalCal g_throttleCal{cfg::kPedalRawReleased, cfg::kPedalRawPressed,
                             cfg::kThrottleDeadbandPct, cfg::kPedalPlausMargin};
kart::PedalCal g_brakeCal{cfg::kPedalRawReleased, cfg::kPedalRawPressed,
                          cfg::kThrottleDeadbandPct, cfg::kPedalPlausMargin};

// ── Wheel state (cached from the USB host on each fresh report) ──
bool g_wheelEnumerated = false;
uint32_t g_lastWheelMs = 0;
uint32_t g_wheelButtons = 0;
uint32_t g_prevButtons = 0;
int g_axis[16] = {0};

// ── Latched outputs honored only at standstill ──
bool g_reverse = false;
bool g_braking = false;

// Firmware's model of the ESC gear (1=LOW, 2=MED, 3=HIGH/turbo). The ESC's
// high-speed line is a single gear-up-cycle button (1->2->3->1 per pulse), so
// we track the gear open-loop and pulse the line to reach the target; if it
// ever drifts from what the FarDriver app shows, resync with `GEAR <low|med|high>`.
enum SpeedMode : uint8_t { kSpeedLow, kSpeedMed, kSpeedHigh };
SpeedMode g_speedMode = kSpeedLow;

// Non-blocking gear-cycle pulser on the high-speed line.
uint8_t g_gearPulsesLeft = 0;   // grounding pulses still to emit
bool g_gearPulseActive = false;  // currently mid-pulse (line grounded)
uint32_t g_gearPulseUntil = 0;   // end of the current pulse/gap phase

// ── DAC health ──
bool g_dacOk = true;
uint32_t g_dacLastOkMs = 0;
uint32_t g_dacOkCount = 0;    // cumulative successful DAC writes
uint32_t g_dacFailCount = 0;  // cumulative failed DAC writes (after retries)
// Bench-only manual DAC hold (SAFE + contactor open): drive a fixed throttle
// voltage to the ESC TPS line for multimeter verification, without arming.
float g_dacManualPct = -1.0f;  // <0 = inactive
uint32_t g_dacManualUntilMs = 0;

// ── Pedal plausibility debounce ──
uint32_t g_implausibleSinceMs = 0;
bool g_implausibleActive = false;

// ── Pi → Teensy one-shot requests ──
bool g_reqDisarm = false;
bool g_reqFaultClear = false;

// ── Telemetry / event bookkeeping ──
uint32_t g_lastTickMs = 0;
uint32_t g_lastTelemetryMs = 0;
uint8_t g_telemetrySeq = 0;
kart::DriveState g_prevState = kart::DriveState::kSafe;
kart::FaultCode g_prevFault = kart::FaultCode::kNone;

float g_throttleCmdPct = 0.0f;
float g_brakePct = 0.0f;
kart::DriveInputs g_lastInputs{};  // snapshot for STATUS visibility

String g_usbRx;
String g_piRx;

#if KART_ENABLE_WATCHDOG
WDT_T4<WDT1> g_wdt;  // WDOG1: resets to SAFE if the control loop stalls
#endif

// ── Hall pulse ISR (glitch-filtered: rejects sub-kHallMinIntervalUs edges,
// e.g. LED-PWM crosstalk coupled onto the hall line) ──
volatile uint32_t g_hallCount = 0;
volatile uint32_t g_lastHallUs = 0;
void hallIsr() {
  uint32_t now = micros();
  if ((uint32_t)(now - g_lastHallUs) >= cfg::kHallMinIntervalUs) {
    g_lastHallUs = now;
    g_hallCount++;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// Low-level output helpers
// ─────────────────────────────────────────────────────────────────────────

void setGroundSwitch(uint8_t pin, bool asserted) {
  digitalWrite(pin, asserted ? HIGH : LOW);
}

// One MCP4725 fast-mode write attempt. Returns true on I2C ACK.
bool dacWriteOnce(uint16_t raw) {
  Wire.beginTransmission(kMcp4725Addr);
  Wire.write(0x40);                          // fast-mode write DAC register
  Wire.write((uint8_t)(raw >> 4));           // D11..D4
  Wire.write((uint8_t)((raw & 0x0F) << 4));  // D3..D0 xxxx
  return Wire.endTransmission() == 0;
}

// Reads back the MCP4725 to see what it actually stored vs what we wrote.
// Returns false if the read fails. `dacval` = current 12-bit DAC register,
// `pd` = power-down bits (0 = normal/output-driven; nonzero = high-Z float).
// Mismatch between commanded and read value, or pd != 0, means the I2C writes
// are being corrupted on the kart bus (vs a clean breadboard).
bool readDac(uint16_t &dacval, uint8_t &pd, uint8_t &settings) {
  if (Wire.requestFrom((int)kMcp4725Addr, 3) != 3) return false;
  settings = (uint8_t)Wire.read();
  uint8_t hi = (uint8_t)Wire.read();
  uint8_t lo = (uint8_t)Wire.read();
  dacval = ((uint16_t)hi << 4) | (lo >> 4);
  pd = (settings >> 1) & 0x03;
  return true;
}

// Writes the DAC and VERIFIES it by reading the value back. The bus is noisy
// when the motor runs (EMI), so a write can NACK *or* be silently corrupted —
// the read-back catches both. Retries (re-initialising Wire to clear a wedged
// transfer) until the DAC is confirmed holding the commanded value with its
// output enabled. Returns true only on a verified write.
bool writeThrottleRaw(uint16_t raw) {
  if (raw > kDacMax) raw = kDacMax;
  for (uint8_t attempt = 0; attempt <= cfg::kDacWriteRetries; attempt++) {
    if (dacWriteOnce(raw)) {
      uint16_t rb;
      uint8_t pd, st;
      if (readDac(rb, pd, st) && pd == 0) {
        uint16_t diff = (rb > raw) ? (rb - raw) : (raw - rb);
        if (diff <= cfg::kDacVerifyTol) return true;  // verified
      }
    }
    Wire.end();
    Wire.begin();
    Wire.setClock(cfg::kI2cClockHz);
  }
  return false;  // could not confirm the commanded value reached the DAC
}

// Maps 0..100 % to the ESC's 0.5–4.3 V throttle window and writes the DAC.
// DAC_ERROR is raised only after the link has failed continuously for
// kDacFailMs (not on a few transient NACKs), so a noisy-but-working bus does
// not fault DRIVE while a genuinely dead DAC still does.
void applyThrottlePercent(float pct) {
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  float v = cfg::kThrottleVMin +
            (cfg::kThrottleVMax - cfg::kThrottleVMin) * (pct / 100.0f);
  float norm = v / cfg::kThrottleDacRef;
  if (norm < 0.0f) norm = 0.0f;
  if (norm > 1.0f) norm = 1.0f;
  uint16_t raw = (uint16_t)roundf(norm * kDacMax);

  uint32_t now = millis();
  if (writeThrottleRaw(raw)) {
    g_dacLastOkMs = now;
    g_dacOk = true;
    g_dacOkCount++;
  } else {
    g_dacFailCount++;
    if ((uint32_t)(now - g_dacLastOkMs) >= cfg::kDacFailMs) g_dacOk = false;
  }
}

void applySafeOutputs() {
  setGroundSwitch(kPinReverse, false);
  setGroundSwitch(kPinBrakeLow, false);
  setGroundSwitch(kPinSpeedHigh, false);
  setGroundSwitch(kPinSpeedLow, false);
  setGroundSwitch(kPinCruise, false);
  setGroundSwitch(kPinContactor, false);
  applyThrottlePercent(0.0f);  // 0.5 V idle floor
}

// ─────────────────────────────────────────────────────────────────────────
// LED strip — drive-state signaling (spec §3.2). Traction-only bench mode is
// signaled loudly so it can never be mistaken for a normal driving state.
// ─────────────────────────────────────────────────────────────────────────

// Channels are driven fully on/off (digitalWrite), never PWM: intermediate
// PWM duty switches the 24 V strip continuously and couples into the hall
// input on pin 2. Solid levels (or low-rate blinks) keep that line clean.
void setLed(bool r, bool g, bool b) {
  digitalWrite(kPinLedRed, r ? HIGH : LOW);
  digitalWrite(kPinLedGreen, g ? HIGH : LOW);
  digitalWrite(kPinLedBlue, b ? HIGH : LOW);
}

void updateLed() {
#if KART_LED_OFF
  setLed(false, false, false);
  return;
#endif
  kart::DriveState s = g_dsm.state();
  bool bench = g_dsm.traction_only_bench();

  switch (s) {
    case kart::DriveState::kSafe:
      // Bench mode = solid magenta (loud "not a drive state"); normal = white.
      if (bench) setLed(true, false, true);
      else setLed(true, true, true);
      break;
    case kart::DriveState::kArmed:
      setLed(true, true, false);  // yellow/amber
      break;
    case kart::DriveState::kDrive:
      // Color encodes the gear: LOW = green, MEDIUM = cyan, HIGH = blue.
      switch (g_speedMode) {
        case kSpeedLow: setLed(false, true, false); break;   // green
        case kSpeedMed: setLed(false, true, true); break;    // cyan
        case kSpeedHigh: setLed(false, false, true); break;  // blue (turbo)
      }
      break;
    case kart::DriveState::kStopping: {
      bool on = (millis() % 300) < 150;  // ~3 Hz blink (not arming-critical)
      setLed(on, on, false);
      break;
    }
    case kart::DriveState::kFault: {
      bool on = (millis() % 400) < 200;  // ~2.5 Hz blink
      setLed(on, false, false);
      break;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────
// Wheel input
// ─────────────────────────────────────────────────────────────────────────

void serviceWheel(uint32_t now) {
  g_usbHost.Task();

  bool enumNow = (bool)g_joystick;
  if (enumNow != g_wheelEnumerated) {
    g_wheelEnumerated = enumNow;
    if (enumNow) {
      char msg[120];
      snprintf(msg, sizeof(msg),
               "INFO WHEEL_CONNECTED vid=0x%04X pid=0x%04X type=%d",
               g_joystick.idVendor(), g_joystick.idProduct(),
               (int)g_joystick.joystickType());
      Serial.println(msg);
      Serial2.println(msg);
      g_lastWheelMs = now;
    } else {
      Serial.println("INFO WHEEL_DISCONNECTED");
      Serial2.println("INFO WHEEL_DISCONNECTED");
      g_wheelButtons = 0;
    }
  }

  if (enumNow && g_joystick.available()) {
    g_wheelButtons = g_joystick.getButtons();
    uint64_t mask = g_joystick.axisMask();
    for (int i = 0; i < 16; i++) {
      if (mask & ((uint64_t)1 << i)) g_axis[i] = g_joystick.getAxis(i);
    }
    g_lastWheelMs = now;
    g_joystick.joystickDataClear();
  }
}

int rawAxis(int idx) {
  return (idx >= 0 && idx < 16) ? g_axis[idx] : 0;
}

bool buttonDown(int idx) { return (g_wheelButtons >> idx) & 1u; }
bool buttonPressedEdge(int idx) {
  return ((g_wheelButtons >> idx) & 1u) && !((g_prevButtons >> idx) & 1u);
}

// ─────────────────────────────────────────────────────────────────────────
// Steering CAN link (Teensy <-> Steervo, docs/protocols/can-ids.md)
// ─────────────────────────────────────────────────────────────────────────

void sendCanStd(uint32_t id, const uint8_t *data, uint8_t len) {
  CAN_message_t msg;
  msg.id = id;
  msg.flags.extended = 0;  // kart traffic is 11-bit standard
  msg.len = len;
  memcpy(msg.buf, data, len);
  g_can.write(msg);
}

// Drain inbound CAN: the only frames we consume are STEER_STATUS heartbeats.
void serviceCan(uint32_t now) {
  CAN_message_t msg;
  while (g_can.read(msg)) {
    if (msg.flags.extended) continue;  // no extended IDs are for us
    if (msg.id == kart::kIdSteerStatus) {
      kart::SteerStatus st;
      if (kart::unpack_steer_status(msg.buf, msg.len, st)) {
        g_steerLink.on_status(st, now);
      }
    }
  }
}

// STEER_SET at 50 Hz. The setpoint streams continuously; the ENABLE bit is set
// only when steering is explicitly armed (`STEER ON`), the wheel is present,
// and the Steervo link is fresh + calibrated + not faulted.
void sendSteerSet(uint32_t now, const kart::DriveInputs &in) {
  if ((uint32_t)(now - g_lastSteerSetMs) < kart::kSteerSetPeriodMs) return;
  g_lastSteerSetMs = now;

  bool enable = g_steerEnabled && in.wheel_connected &&
                g_steerLink.link_ok(now) && g_steerLink.calibrated() &&
                !g_steerLink.reports_fault();

  kart::SteerSet s{enable, g_steerSetpointCdeg, g_steerLink.next_seq()};
  uint8_t buf[kart::kSteerSetDlc];
  kart::pack_steer_set(s, buf);
  sendCanStd(kart::kIdSteerSet, buf, sizeof(buf));
}

void sendSteerCal(kart::SteerCalCmd cmd) {
  uint8_t buf[kart::kSteerCalDlc];
  kart::pack_steer_cal(cmd, buf);
  sendCanStd(kart::kIdSteerCal, buf, sizeof(buf));
}

void sendSteerCfg(kart::SteerCfgParam param, float value) {
  kart::SteerCfg m{param, value};
  uint8_t buf[kart::kSteerCfgDlc];
  kart::pack_steer_cfg(m, buf);
  sendCanStd(kart::kIdSteerCfg, buf, sizeof(buf));
}

// ─────────────────────────────────────────────────────────────────────────
// Per-tick input gathering and output application
// ─────────────────────────────────────────────────────────────────────────

kart::DriveInputs gatherInputs(uint32_t now) {
  kart::PedalMap throttleMap(g_throttleCal);
  kart::PedalMap brakeMap(g_brakeCal);

  int rawThrottle = rawAxis(g_axisThrottle);
  int rawBrake = rawAxis(g_axisBrake);
  g_throttleCmdPct = throttleMap.map(rawThrottle);
  g_brakePct = brakeMap.map(rawBrake);

  // The Hori only emits a USB report when something changes, so report
  // freshness is NOT a disconnect signal — held buttons/pedals simply keep
  // their last reported value (exactly what we want). USB-host enumeration is
  // the connection truth: (bool)g_joystick goes false the instant it unplugs.
  bool wheelOk = g_wheelEnumerated;

  // Pedal plausibility debounce (>kPedalImplausibleMs of OOR readings).
  bool plausibleNow =
      throttleMap.plausible(rawThrottle) && brakeMap.plausible(rawBrake);
  if (plausibleNow) {
    g_implausibleActive = false;
  } else if (!g_implausibleActive) {
    g_implausibleActive = true;
    g_implausibleSinceMs = now;
  }
  bool pedalImplausible =
      g_implausibleActive &&
      (uint32_t)(now - g_implausibleSinceMs) >= cfg::kPedalImplausibleMs;

  g_hall.update(g_hallCount, now);

  // Steering setpoint from the wheel axis (sent every tick via STEER_SET).
  g_steerSetpointCdeg = g_steerLink.axis_to_setpoint(rawAxis(g_axisSteer));

  kart::DriveInputs in{};
  in.wheel_connected = wheelOk;
  // Real Steervo health from its STEER_STATUS heartbeat. In traction-only bench
  // mode the drive state machine ignores these (steering declared absent for
  // the traction DRIVE gate); they still drive STATUS/telemetry + the steering
  // enable gate.
  in.steer_link_ok = g_steerLink.link_ok(now);
  in.steer_calibrated = g_steerLink.calibrated();
  in.steer_fault = g_steerLink.reports_fault();
  in.pedal_plausible = wheelOk ? !pedalImplausible : true;
  in.dac_ok = g_dacOk;
  in.contactor_ok = !g_contactor.faulted();
  in.throttle_at_zero = g_throttleCmdPct <= cfg::kThrottleZeroPct;
  in.vehicle_stopped = g_hall.stopped(now);
  in.bus_ready = g_contactor.ready();

  // ── Operator requests ──
  kart::ArmChordInputs chord{};
  chord.paddle_left = buttonDown(cfg::kBtnPaddleLeft);
  chord.paddle_right = buttonDown(cfg::kBtnPaddleRight);
  chord.brake_pressed = g_brakePct >= cfg::kBrakeThresholdPct;
  chord.throttle_released = in.throttle_at_zero;
  in.arm_confirmed = wheelOk && g_armChord.update(chord, now);

  // The DRIVE button is context-sensitive so the driver can run the whole
  // flow from the wheel: ARMED -> enter DRIVE, DRIVE -> disarm (stop),
  // FAULT -> clear. (DISARM/FAULT_CLEAR also remain available over UART.)
  bool driveBtnEdge = buttonPressedEdge(cfg::kBtnDrive);
  kart::DriveState st = g_dsm.state();
  in.drive_requested = driveBtnEdge && st == kart::DriveState::kArmed;
  in.disarm_requested = g_reqDisarm || buttonPressedEdge(cfg::kBtnDisarm) ||
                        (driveBtnEdge && st == kart::DriveState::kDrive);
  in.fault_clear_requested = g_reqFaultClear ||
                             buttonPressedEdge(cfg::kBtnFaultClear) ||
                             (driveBtnEdge && st == kart::DriveState::kFault);

  g_reqDisarm = false;
  g_reqFaultClear = false;
  return in;
}

void applyOutputs(const kart::DriveInputs &in, uint32_t now) {
  kart::DriveOutputs out = g_dsm.outputs();
  kart::DriveState s = g_dsm.state();

  // Contactor sequencing: the state machine asks for the bus, the sequencer
  // closes pin 32 after the settle dwell.
  g_contactor.update(out.contactor_closed, now);
  setGroundSwitch(kPinContactor, g_contactor.contactor_closed());

  // Brake: forced during a controlled stop, or live from the pedal in DRIVE.
  // Brake always overrides throttle.
  bool pedalBrake = (s == kart::DriveState::kDrive) &&
                    (g_brakePct >= cfg::kBrakeThresholdPct);
  g_braking = out.brake_assert || pedalBrake;
  setGroundSwitch(kPinBrakeLow, g_braking);

  // Bench manual DAC hold expires automatically and is ONLY honored in SAFE
  // (where the contactor is commanded open — so no motion is possible).
  bool manual_expired = (int32_t)(now - g_dacManualUntilMs) >= 0;  // now >= until
  if (g_dacManualPct >= 0.0f &&
      (s != kart::DriveState::kSafe || manual_expired)) {
    g_dacManualPct = -1.0f;  // cancel on leaving SAFE or on timeout
  }

  // Throttle DAC: live in DRIVE (pedal, slew-limited, not braking), the bench
  // manual hold in SAFE, otherwise pinned to the 0.5 V idle floor.
  if (out.throttle_allowed && !g_braking) {
    float cmd = g_throttleSlew.update(g_throttleCmdPct, cfg::kTickPeriodMs);
    applyThrottlePercent(cmd);
    g_throttleCmdPct = cmd;
  } else if (g_dacManualPct >= 0.0f && s == kart::DriveState::kSafe) {
    applyThrottlePercent(g_dacManualPct);  // bench measurement, contactor open
    g_throttleCmdPct = g_dacManualPct;
  } else {
    g_throttleSlew.reset(0.0f);
    g_throttleCmdPct = 0.0f;
    applyThrottlePercent(0.0f);
  }

  bool stopped = in.vehicle_stopped;
  bool active = (s == kart::DriveState::kArmed || s == kart::DriveState::kDrive);

  // Reverse: X button toggles the driver's intent, only at standstill. The
  // physical REV line is XOR'd with kInvertDirection because the motor's
  // learned forward is physically backwards on this kart.
  if (active && stopped && buttonPressedEdge(2 /* X */)) {
    g_reverse = !g_reverse;
  }
  if (!active) g_reverse = false;
  setGroundSwitch(kPinReverse, g_reverse != cfg::kInvertDirection);

  // Gear shifting via the shift paddles (armed/driving only): right = upshift,
  // left = downshift. The ESC gear-cycle button only goes up (1->2->3->1), so
  // an upshift is 1 pulse and a downshift is 2 pulses (== down one in a 3-gear
  // wrap), clamped so we never wrap past the ends. A new shift is accepted only
  // when no pulse sequence is in flight. (Single-paddle taps here never collide
  // with the arm chord, which is both paddles + brake in SAFE where active=0.)
  bool gearIdle = g_gearPulsesLeft == 0 && !g_gearPulseActive;
  if (active && gearIdle) {
    if (buttonPressedEdge(cfg::kBtnPaddleRight) && g_speedMode < kSpeedHigh) {
      g_speedMode = (SpeedMode)(g_speedMode + 1);
      g_gearPulsesLeft = 1;  // up one
    } else if (buttonPressedEdge(cfg::kBtnPaddleLeft) &&
               g_speedMode > kSpeedLow) {
      g_speedMode = (SpeedMode)(g_speedMode - 1);
      g_gearPulsesLeft = 2;  // down one == cycle up twice
    }
  }

  // Advance the non-blocking pulser: ground the high-speed line for kGearPulseMs,
  // release for kGearPulseGapMs, once per remaining pulse.
  if (g_gearPulsesLeft > 0 || g_gearPulseActive) {
    if ((int32_t)(now - g_gearPulseUntil) >= 0) {
      if (g_gearPulseActive) {
        g_gearPulseActive = false;
        g_gearPulsesLeft--;
        g_gearPulseUntil = now + cfg::kGearPulseGapMs;
      } else if (g_gearPulsesLeft > 0) {
        g_gearPulseActive = true;
        g_gearPulseUntil = now + cfg::kGearPulseMs;
      }
    }
  }
  setGroundSwitch(kPinSpeedHigh, g_gearPulseActive);
  setGroundSwitch(kPinSpeedLow, false);  // not a gear input on this ESC
}

// ─────────────────────────────────────────────────────────────────────────
// Telemetry + events
// ─────────────────────────────────────────────────────────────────────────

uint16_t statusFlags(const kart::DriveInputs &in) {
  uint16_t f = 0;
  if (in.wheel_connected) f |= kart::kFlagWheelConnected;
  if (in.steer_link_ok) f |= kart::kFlagSteerLinkOk;
  if (in.steer_calibrated) f |= kart::kFlagSteerCalibrated;
  if (g_contactor.contactor_closed()) f |= kart::kFlagContactorClosed;
  if (g_reverse) f |= kart::kFlagReverse;
  if (g_braking) f |= kart::kFlagBrakeActive;
  if (g_dsm.traction_only_bench()) f |= kart::kFlagBenchMode;
  return f;
}

void sendTelemetry(uint32_t now, const kart::DriveInputs &in) {
  kart::TelemetryV1 t{};
  t.drive_state = (uint8_t)g_dsm.state();
  t.fault_code = (uint8_t)g_dsm.fault();
  t.status_flags = statusFlags(in);
  t.throttle_pct = (uint8_t)(g_throttleCmdPct + 0.5f);
  t.brake_pct = (uint8_t)(g_brakePct + 0.5f);
  t.steer_setpoint_cdeg = g_steerSetpointCdeg;
  t.steer_measured_cdeg = g_steerLink.last_status().measured_cdeg;
  t.hall_count = g_hallCount;
  t.hall_hz_x10 = g_hall.hz_x10();
  t.batt_dv = 0;
  t.batt_da = 0;
  t.esc_rpm = 0;
  t.controller_temp_c = kart::kTempUnknown;
  t.motor_temp_c = kart::kTempUnknown;
  t.uptime_ms = now;
  t.seq = g_telemetrySeq++;

  uint8_t frame[kart::kTelemetryV1FrameLen];
  size_t n = kart::encode_telemetry_v1(t, frame, sizeof(frame));
  Serial2.write(frame, n);
}

const char *stateName(kart::DriveState s) {
  switch (s) {
    case kart::DriveState::kSafe: return "SAFE";
    case kart::DriveState::kArmed: return "ARMED";
    case kart::DriveState::kDrive: return "DRIVE";
    case kart::DriveState::kStopping: return "STOPPING";
    case kart::DriveState::kFault: return "FAULT";
  }
  return "?";
}

// Mirror state/fault transitions as INFO lines for terminal users
// (uart-protocol.md §2: events are also mirrored as text).
void reportTransitions() {
  kart::DriveState s = g_dsm.state();
  if (s != g_prevState) {
    char msg[64];
    snprintf(msg, sizeof(msg), "INFO STATE %s", stateName(s));
    Serial.println(msg);
    Serial2.println(msg);
    g_prevState = s;
  }
  kart::FaultCode fc = g_dsm.fault();
  if (fc != g_prevFault) {
    char msg[48];
    snprintf(msg, sizeof(msg), "INFO FAULT %d", (int)fc);
    Serial.println(msg);
    Serial2.println(msg);
    g_prevFault = fc;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// Command channel
// ─────────────────────────────────────────────────────────────────────────

bool inSafe() { return g_dsm.state() == kart::DriveState::kSafe; }

const char *steerStateName(kart::SteerState s) {
  switch (s) {
    case kart::SteerState::kInit: return "INIT";
    case kart::SteerState::kReady: return "READY";
    case kart::SteerState::kActive: return "ACTIVE";
    case kart::SteerState::kFault: return "FAULT";
    case kart::SteerState::kCalibrating: return "CAL";
  }
  return "?";
}

void cmdSteer(Stream &out) {
  const kart::SteerStatus &st = g_steerLink.last_status();
  uint32_t now = millis();
  out.print("OK STEER enabled=");
  out.print(g_steerEnabled ? 1 : 0);
  out.print(" link=");
  out.print(g_steerLink.link_ok(now) ? 1 : 0);
  out.print(" sv_state=");
  out.print(g_steerLink.have_status() ? steerStateName(st.state) : "--");
  out.print(" cal=");
  out.print(g_steerLink.calibrated() ? 1 : 0);
  out.print(" fault_bits=0x");
  out.print(st.fault_bits, HEX);
  out.print(" set_cdeg=");
  out.print(g_steerSetpointCdeg);
  out.print(" meas_cdeg=");
  out.print(st.measured_cdeg);
  out.print(" out_pct=");
  out.print(st.output_pct);
  out.print(" pot=");
  out.println(st.pot_raw);
}

void cmdWheelRaw(Stream &out) {
  out.print("OK WHEELRAW enum=");
  out.print(g_wheelEnumerated ? 1 : 0);
  out.print(" type=");
  out.print((int)g_joystick.joystickType());
  out.print(" buttons=0x");
  out.print(g_wheelButtons, HEX);
  for (int i = 0; i < 16; i++) {
    out.print(" a");
    out.print(i);
    out.print('=');
    out.print(g_axis[i]);
  }
  out.println();
}

void cmdStatus(Stream &out) {
  out.print("OK STATUS state=");
  out.print(stateName(g_dsm.state()));
  out.print(" fault=");
  out.print((int)g_dsm.fault());
  out.print(" bench=");
  out.print(g_dsm.traction_only_bench() ? 1 : 0);
  out.print(" wheel=");
  out.print(g_wheelEnumerated ? 1 : 0);
  out.print(" thr=");
  out.print(g_throttleCmdPct, 1);
  out.print(" brk=");
  out.print(g_brakePct, 1);
  out.print(" hall=");
  out.print(g_hallCount);
  out.print(" hz10=");
  out.print(g_hall.hz_x10());
  out.print(" contactor=");
  out.print(g_contactor.contactor_closed() ? 1 : 0);
  out.print(" rev=");
  out.print(g_reverse ? 1 : 0);
  out.print(" speed=");
  out.print(g_speedMode == kSpeedHigh ? "high"
            : g_speedMode == kSpeedLow ? "low"
                                       : "med");
  // Health / arm-gate visibility (1 = ok/satisfied).
  out.print(" dac=");
  out.print(g_dacOk ? 1 : 0);
  out.print(" dacok=");
  out.print(g_dacOkCount);
  out.print(" dacfail=");
  out.print(g_dacFailCount);
  out.print(" plaus=");
  out.print(g_lastInputs.pedal_plausible ? 1 : 0);
  out.print(" busrdy=");
  out.print(g_lastInputs.bus_ready ? 1 : 0);
  out.print(" vstop=");
  out.print(g_lastInputs.vehicle_stopped ? 1 : 0);
  out.print(" thr0=");
  out.print(g_lastInputs.throttle_at_zero ? 1 : 0);
  out.print(" wheelok=");
  out.print(g_lastInputs.wheel_connected ? 1 : 0);
  out.print(" steer_en=");
  out.print(g_steerEnabled ? 1 : 0);
  out.print(" steer_link=");
  out.print(g_lastInputs.steer_link_ok ? 1 : 0);
  out.print(" steer_cal=");
  out.println(g_lastInputs.steer_calibrated ? 1 : 0);
}

// Scans the MCP4725's I2C bus and lists every address that ACKs, so a missing
// or flaky DAC is obvious. Read-only.
void cmdI2cScan(Stream &out) {
  out.print("OK I2C found");
  int n = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      out.print(" 0x");
      out.print(addr, HEX);
      n++;
    }
  }
  if (n == 0) out.print(" (none)");
  out.print(" dac@0x");
  out.print(kMcp4725Addr, HEX);
  out.println();
}

void cmdCfgShow(Stream &out) {
  out.print("OK CFG slew_rise=");
  out.print(cfg::kThrottleSlewRisePerS, 1);
  out.print(" axis_thr=");
  out.print(g_axisThrottle);
  out.print(" axis_brk=");
  out.print(g_axisBrake);
  out.print(" axis_steer=");
  out.print(g_axisSteer);
  out.print(" ped_released=");
  out.print(g_throttleCal.raw_released);
  out.print(" ped_pressed=");
  out.println(g_throttleCal.raw_pressed);
}

void cmdCfgSet(const String &name, const String &value, Stream &out) {
  if (!inSafe()) {
    out.println("ERR NOT_SAFE (CFG only in SAFE)");
    return;
  }
  long v = value.toInt();
  if (name == "axis_thr") {
    g_axisThrottle = (int)v;
  } else if (name == "axis_brk") {
    g_axisBrake = (int)v;
  } else if (name == "axis_steer") {
    g_axisSteer = (int)v;
  } else if (name == "ped_released") {
    g_throttleCal.raw_released = g_brakeCal.raw_released = (int32_t)v;
  } else if (name == "ped_pressed") {
    g_throttleCal.raw_pressed = g_brakeCal.raw_pressed = (int32_t)v;
  } else {
    out.println("ERR UNKNOWN_CFG");
    return;
  }
  out.print("OK CFG ");
  out.print(name);
  out.print('=');
  out.println(v);
}

void handleCommand(const String &line, Stream &out) {
  if (line.length() == 0) return;

  if (line == "PING") {
    out.println("OK PONG");
  } else if (line == "VERSION") {
    out.print("OK VERSION kart-core ");
    out.print(kVersion);
    out.println(" proto=1");
  } else if (line == "STATUS") {
    cmdStatus(out);
  } else if (line == "WHEELRAW") {
    cmdWheelRaw(out);
  } else if (line == "I2C") {
    cmdI2cScan(out);
  } else if (line.startsWith("GEAR")) {
    // Resync the firmware's gear model to what the FarDriver app shows (the
    // gear is tracked open-loop). Does not pulse — just sets the model.
    String a = line.substring(4);
    a.trim();
    if (a == "low" || a == "1") g_speedMode = kSpeedLow;
    else if (a == "med" || a == "2") g_speedMode = kSpeedMed;
    else if (a == "high" || a == "3") g_speedMode = kSpeedHigh;
    else { out.println("ERR GEAR (low|med|high)"); return; }
    out.print("OK GEAR ");
    out.println(g_speedMode == kSpeedHigh ? "high"
                : g_speedMode == kSpeedMed ? "med"
                                           : "low");
  } else if (line == "DACREAD") {
    uint16_t v = 0; uint8_t pd = 0, st = 0;
    if (readDac(v, pd, st)) {
      out.print("OK DACREAD dacval=");
      out.print(v);
      out.print("/4095 pd=");
      out.print(pd);
      out.print(pd == 0 ? " (normal)" : " (POWER-DOWN: output high-Z!)");
      out.print(" settings=0x");
      out.println(st, HEX);
    } else {
      out.println("ERR DACREAD (I2C read failed)");
    }
  } else if (line.startsWith("DACSET")) {
    // Bench-only: hold a fixed throttle DAC voltage in SAFE for metering.
    // Refused outside SAFE so it can never command motion.
    if (g_dsm.state() != kart::DriveState::kSafe) {
      out.println("ERR NOT_SAFE (DACSET only in SAFE, contactor open)");
    } else {
      float pct = 0.0f;
      int sp = line.indexOf(' ');
      if (sp > 0) pct = line.substring(sp + 1).toFloat();
      if (pct < 0.0f) pct = 0.0f;
      if (pct > 100.0f) pct = 100.0f;
      g_dacManualPct = pct;
      g_dacManualUntilMs = millis() + 10000;  // auto-clears after 10 s
      out.print("OK DACSET ");
      out.print(pct, 1);
      out.println("% for 10s (SAFE only)");
    }
  } else if (line == "ARM_REQ") {
    // Advisory only: real arming is the driver wheel chord (spec §3.1).
    out.println("OK ARM_REQ pending (complete the wheel chord to arm)");
  } else if (line == "DISARM" || line == "SAFE") {
    g_reqDisarm = true;
    out.println(line == "SAFE" ? "OK SAFE" : "OK DISARMED");
  } else if (line == "FAULT_CLEAR") {
    if (g_dsm.state() != kart::DriveState::kFault) {
      out.println("ERR NO_FAULT");
    } else {
      g_reqFaultClear = true;
      out.println("OK FAULT_CLEAR pending");
    }
  } else if (line == "CFG?") {
    cmdCfgShow(out);
  } else if (line.startsWith("CFG ")) {
    int sp = line.indexOf(' ', 4);
    if (sp < 0) {
      out.println("ERR CFG_SYNTAX (CFG <name> <value>)");
    } else {
      cmdCfgSet(line.substring(4, sp), line.substring(sp + 1), out);
    }
  } else if (line.startsWith("LED ")) {
    if (!inSafe()) {
      out.println("ERR NOT_SAFE (LED only in SAFE)");
    } else {
      out.println("OK LED (drive-state signaling resumes on state change)");
    }
  } else if (line == "STEER") {
    cmdSteer(out);
  } else if (line.startsWith("STEER ")) {
    String a = line.substring(6);
    a.trim();
    if (a == "ON") {
      g_steerEnabled = true;
      out.println("OK STEER ON (tracks wheel once link is healthy + calibrated; "
                  "the ESP32 kEnableMotorOutput gate must also be set)");
    } else if (a == "OFF") {
      g_steerEnabled = false;
      out.println("OK STEER OFF");
    } else if (a.startsWith("CAL")) {
      // Calibration is only valid stationary; require SAFE (can-ids.md §0x102).
      if (!inSafe()) {
        out.println("ERR NOT_SAFE (STEER CAL only in SAFE)");
        return;
      }
      String c = a.substring(3);
      c.trim();
      kart::SteerCalCmd cmd;
      if (c == "ENTER") cmd = kart::SteerCalCmd::kEnter;
      else if (c == "CENTER") cmd = kart::SteerCalCmd::kMarkCenter;
      else if (c == "LEFT") cmd = kart::SteerCalCmd::kMarkLeft;
      else if (c == "RIGHT") cmd = kart::SteerCalCmd::kMarkRight;
      else if (c == "SAVE") cmd = kart::SteerCalCmd::kSaveExit;
      else if (c == "ABORT") cmd = kart::SteerCalCmd::kAbort;
      else {
        out.println("ERR STEER CAL (ENTER|CENTER|LEFT|RIGHT|SAVE|ABORT)");
        return;
      }
      sendSteerCal(cmd);
      out.print("OK STEER CAL ");
      out.println(c);
    } else if (a.startsWith("CFG")) {
      String rest = a.substring(3);
      rest.trim();
      int sp = rest.indexOf(' ');
      if (sp < 0) {
        out.println("ERR STEER CFG (KP|KI|KD|LIM|MARGIN <value>)");
        return;
      }
      String p = rest.substring(0, sp);
      float v = rest.substring(sp + 1).toFloat();
      kart::SteerCfgParam param;
      if (p == "KP") param = kart::SteerCfgParam::kKp;
      else if (p == "KI") param = kart::SteerCfgParam::kKi;
      else if (p == "KD") param = kart::SteerCfgParam::kKd;
      else if (p == "LIM") param = kart::SteerCfgParam::kOutputLimitPct;
      else if (p == "MARGIN") param = kart::SteerCfgParam::kSoftLimitMarginCdeg;
      else {
        out.println("ERR STEER CFG param (KP|KI|KD|LIM|MARGIN)");
        return;
      }
      sendSteerCfg(param, v);
      out.print("OK STEER CFG ");
      out.print(p);
      out.print('=');
      out.println(v);
    } else {
      out.println("ERR STEER (ON|OFF|CAL ...|CFG ...)");
    }
  } else {
    out.println("ERR UNKNOWN_CMD");
  }
}

void servicePort(Stream &port, String &buffer) {
  while (port.available() > 0) {
    char c = (char)port.read();
    if (c == '\n' || c == '\r') {
      String line = buffer;
      buffer = "";
      line.trim();
      handleCommand(line, port);
    } else if (buffer.length() < 180) {
      buffer += c;
    }
  }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────

void setup() {
  pinMode(kPinHallPulses, INPUT_PULLUP);
  pinMode(kPinPps, INPUT);
  pinMode(kPinReverse, OUTPUT);
  pinMode(kPinBrakeLow, OUTPUT);
  pinMode(kPinSpeedHigh, OUTPUT);
  pinMode(kPinSpeedLow, OUTPUT);
  pinMode(kPinCruise, OUTPUT);
  pinMode(kPinContactor, OUTPUT);
  pinMode(kPinLedRed, OUTPUT);
  pinMode(kPinLedGreen, OUTPUT);
  pinMode(kPinLedBlue, OUTPUT);

  Wire.begin();
  Wire.setClock(cfg::kI2cClockHz);
  applySafeOutputs();

  Serial.begin(115200);
  Serial2.begin(115200);

  g_usbHost.begin();
  attachInterrupt(digitalPinToInterrupt(kPinHallPulses), hallIsr, RISING);

  // Steering CAN bus (CAN3, mainboard pins 30/31). 1 Mbps per can-ids.md.
  // FIFO (accept-all) so read() polls inbound STEER_STATUS without per-mailbox
  // RX setup; write() still uses the TX mailboxes.
  g_can.begin();
  g_can.setBaudRate(kart::kCanBitrate);
  g_can.setMaxMB(16);
  g_can.enableFIFO();

#if KART_TRACTION_ONLY_BENCH
  g_dsm.set_traction_only_bench(true);
#endif

#if KART_ENABLE_WATCHDOG
  WDT_timings_t wdtCfg;
  wdtCfg.timeout = (float)cfg::kWatchdogTimeoutMs / 1000.0f;
  g_wdt.begin(wdtCfg);
#endif

  Serial2.print("INFO BOOT kart-core ");
  Serial2.print(kVersion);
#if KART_TRACTION_ONLY_BENCH
  Serial2.println(" *** TRACTION-ONLY BENCH (STANDS ONLY) — steering present but "
                  "gated; enable with STEER ON ***");
#else
  Serial2.println(" (full-authority build)");
#endif
}

void loop() {
  uint32_t now = millis();

  serviceWheel(now);
  serviceCan(now);

  if ((uint32_t)(now - g_lastTickMs) >= cfg::kTickPeriodMs) {
    g_lastTickMs = now;

    kart::DriveInputs in = gatherInputs(now);
    g_lastInputs = in;
    g_dsm.tick(in, now);
    applyOutputs(in, now);
    sendSteerSet(now, in);
    updateLed();
    reportTransitions();
    g_prevButtons = g_wheelButtons;

    if ((uint32_t)(now - g_lastTelemetryMs) >= cfg::kTelemetryPeriodMs) {
      g_lastTelemetryMs = now;
      sendTelemetry(now, in);
    }
  }

  servicePort(Serial, g_usbRx);
  servicePort(Serial2, g_piRx);

#if KART_ENABLE_WATCHDOG
  g_wdt.feed();
#endif
}
