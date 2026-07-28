// kart-core build configuration — compile-time flags and bench tunables.
//
// Change a value here and reflash. Safety-relevant flags are intentionally
// loud (the boot banner echoes them and the LED strip signals bench mode).
#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────
// TRACTION-ONLY BENCH MODE (spec §3.6)
//
//   1 = stands-only traction bring-up. The steering-health DRIVE-entry gate is
//       removed and the two Steervo watchdog faults are suppressed. NO steering
//       authority exists in this build. VALID ONLY WITH THE DRIVEN WHEELS OFF
//       THE GROUND.
//   0 = normal full-authority build (steering health required to arm/drive).
//
// This MUST be 0 for any image that will ever touch the ground. It is 1 here
// because the Steervo is away for repair and traction is being brought up on
// stands (see SOFTWARE-STACK-PLAN.md §8). Reverting is this one line + reflash.
// ─────────────────────────────────────────────────────────────────────────
#ifndef KART_TRACTION_ONLY_BENCH
#define KART_TRACTION_ONLY_BENCH 1
#endif

// Hardware watchdog (WDOG1). Resets to SAFE if the loop stalls. Disable only
// for low-level debugging.
#ifndef KART_ENABLE_WATCHDOG
#define KART_ENABLE_WATCHDOG 1
#endif

// Debug: force the LED strip fully off (rules out 24 V LED load as a power
// disturbance). Normal builds leave this 0.
#ifndef KART_LED_OFF
#define KART_LED_OFF 0
#endif

namespace kart {
namespace cfg {

// ── Control timing ──
constexpr uint32_t kTickPeriodMs = 10;        // 100 Hz control tick
constexpr uint32_t kTelemetryPeriodMs = 50;   // 20 Hz telemetry
constexpr uint32_t kWatchdogTimeoutMs = 500;  // WDOG1 (loop must feed faster)

// ── Throttle path (T1) ──
// Throttle ramp DISABLED (0 = instant rise): the pedal maps straight through so
// response is 1:1 for ground driving. A conservative rise ramp (was 25 %/s) was
// an early bench safeguard against lurching on stands; it's no longer wanted now
// that we're driving. Re-enable by setting a positive %/s here if ever needed.
constexpr float kThrottleSlewRisePerS = 0.0f;   // %/s (0 = instant, no ramp)
constexpr float kThrottleSlewFallPerS = 0.0f;   // instant cut on lift (0 = inf)
constexpr float kThrottleDeadbandPct = 3.0f;
// MCP4725: 0.5 V (idle) .. 4.3 V (full) on a 5.0 V reference, 12-bit.
constexpr float kThrottleVMin = 0.5f;
constexpr float kThrottleVMax = 4.3f;
constexpr float kThrottleDacRef = 5.0f;
// I2C bus speed for the MCP4725. The DAC sits on a header/breakout, so keep
// this conservative — 100 kHz like the legacy firmware (400 kHz proved flaky).
constexpr uint32_t kI2cClockHz = 100000;
// DAC write robustness: the MCP4725's I2C link goes intermittently noisy once
// the ESC powers it (observed flicker of ACK/NACK). Retry each write a few
// times (with a bus re-init between tries) and only raise DAC_ERROR after the
// link has been *continuously* failing for kDacFailMs — so transient NACKs
// don't kill DRIVE, but a genuinely dead DAC still faults.
constexpr uint8_t kDacWriteRetries = 4;
constexpr uint32_t kDacFailMs = 500;
// Each DAC write is read back and verified (catches silent I2C corruption from
// motor EMI, not just NACKs). A readback within this many LSB of the commanded
// value, with power-down off, counts as verified. Big corruptions (wrong high
// bits / power-down) are rejected and retried.
constexpr uint16_t kDacVerifyTol = 16;

// ── Brake ──
// The brake line is a HELD level into the ESC's regen brake, but it engages the
// ESC's highest regen level in one step — so the pedal is treated as a switch,
// not a proportional command: past this % the brake line is grounded, below it
// released. Also the "brake pressed" leg of the ARM chord. See applyOutputs.
constexpr float kBrakeThresholdPct = 20.0f;  // pedal % that engages regen brake

// ── Gear shifting ──
// This ESC's high-speed line is a momentary gear-cycle button: each grounding
// pulse advances the gear 1->2->3->1. So an upshift = 1 pulse, and a downshift
// = 2 pulses (down-one == up-twice in a 3-gear wrap). These set the pulse width
// and the gap between pulses (the ESC registers each grounding edge as a press).
constexpr uint32_t kGearPulseMs = 80;
constexpr uint32_t kGearPulseGapMs = 80;

// ── Drivetrain direction ──
// Direction is handled in the FarDriver app (motor direction parameter), so the
// firmware leaves the REV line at its natural sense: driver-forward = REV
// released, driver-reverse = REV asserted. Set true only if you ever want the
// firmware to swap forward/reverse instead of the app (don't do both).
constexpr bool kInvertDirection = false;

// ── Steering wheel→motion sense ──
// Bench-confirmed (July 2026): on this build a LEFT wheel turn drove the
// steering RIGHT, so the command sense is reversed here. The closed-loop sign
// (motor leads) and the pot calibration are independent of this — leave those
// alone; this only flips which way a given wheel deflection commands.
constexpr bool kSteerInvertDirection = true;

// ── Pedal / wheel "at zero" and plausibility ──
constexpr float kThrottleZeroPct = 2.0f;        // <= this counts as "released"
constexpr uint32_t kPedalImplausibleMs = 250;   // debounce before faulting
constexpr uint32_t kWheelStaleMs = 100;         // no fresh report -> disconnect

// ── Contactor sequencing (T2) ──
// Precharge is a Teensy-driven resistor on GPIO27 (HIGH = energized). The bus
// bring-up is precharge 2 s -> contactor closed -> settle -> ready.
// SAFETY: the resistor overheats and melts its enclosure if left on. Never
// raise kPrechargeMs/kPrechargeMaxMs beyond a few seconds, and never lower the
// cooldown enough to let arm/disarm cycling duty-cycle it hot.
constexpr uint32_t kPrechargeMs = 4000;            // resistor on before closing
constexpr uint32_t kPrechargeMaxMs = 5000;         // hard cap -> shed + fault
constexpr uint32_t kPrechargeCooldownMs = 10000;   // min off-time between cycles
constexpr uint32_t kContactorSettleMs = 500;

// ── Speed sensing (T3) ──
// The speedometer reads the ESC's SPD "digital speed pulse" output (ESC pin 13
// -> Teensy pin 22), NOT the pin-2 "hall pulses" line. Bench-confirmed (July
// 2026): pin 2 emits a fixed ~340 Hz signal whenever the ESC is energized —
// present even with the wheel dead still — so it does not track road speed.
// SPD is silent at rest and produces clean pulses once the wheel turns
// (configure the ESC for 6 pulses + "isolated pulse"). See docs/speedometer.md.
//
// The window is a bit longer than a raw-tach default to average out low-speed
// cogging bursts (on stands the motor lurches, so SPD arrives in packets with
// gaps up to ~30 ms). The stop timeout is deliberately conservative: only after
// this long with NO SPD pulse is the vehicle called stopped, so an inter-burst
// gap (or a brief coast below SPD's threshold) is never mistaken for stopped.
constexpr uint32_t kHallWindowMs = 250;
constexpr uint32_t kHallStopTimeoutMs = 500;
// Glitch filter for both tach ISRs (rejects sub-interval edges): see below.
// Hall ISR glitch filter: ignore edges closer than this. Rejects isolated
// glitches/crosstalk while passing real hall pulses. Max admissible pulse
// rate = 1e6 / kHallMinIntervalUs Hz. (Continuous noise — e.g. LED PWM — is
// handled at the source: the LED strip is driven on/off only, never PWM.)
constexpr uint32_t kHallMinIntervalUs = 120;  // -> up to ~8.3 kHz real pulses

// ── ARM chord (spec §3.1) ──
constexpr uint32_t kArmChordHoldMs = 1000;

// ── Hori wheel axis map (USB host) ──
// HARDWARE-CONFIRMED via WHEELRAW on the Teensy USBHost_t36 path (Hori
// enumerates as Xbox One, joystickType 3). NOTE: these differ from the
// Linux-js values in steering-wheel.md — the triggers are 0..1023 unsigned,
// not ±32767, and the indices are shifted. Re-confirm with WHEELRAW if the
// wheel firmware or USBHost_t36 version changes.
constexpr int kAxisSteer = 0;     // a0: -32768 (full L) .. +32767 (full R)
constexpr int kAxisBrake = 3;     // a3: 0 released .. 1023 pressed
constexpr int kAxisThrottle = 4;  // a4: 0 released .. 1023 pressed
// Pedal calibration: raw axis value released vs. fully pressed (both triggers).
constexpr int32_t kPedalRawReleased = 0;
constexpr int32_t kPedalRawPressed = 1023;
constexpr int32_t kPedalPlausMargin = 256;

// ── Wheel button bit indices (USBHost_t36 decode) ──
// Face buttons re-confirmed on the bench (July 2026): the Hori reports them on
// consecutive bits — A=4, B=5, X=6, Y=7. Direction (Reverse) is no longer a
// face-button toggle: it is the bottom rung of the paddle shift ladder
// (Reverse < Park < Low < Med < High), so nothing reads bit 2 anymore. See
// shift_ladder.h / applyOutputs. Disarm/fault-clear are also on the UART
// (DISARM / FAULT_CLEAR).
constexpr int kBtnPaddleLeft = 12;   // left shift paddle -> downshift
constexpr int kBtnPaddleRight = 13;  // right shift paddle -> upshift
constexpr int kBtnDrive = 6;         // physical X face button -> enter DRIVE
constexpr int kBtnDisarm = 5;        // physical B face button -> disarm (off)
constexpr int kBtnFaultClear = 8;    // placeholder (use UART FAULT_CLEAR)

}  // namespace cfg
}  // namespace kart
