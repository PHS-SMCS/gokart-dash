// kart-core — Teensy 4.1 motion-authority firmware (Phase 0 scaffold).
//
// This shell boots into a deterministic safe state, runs the drive state
// machine at 100 Hz with conservative stub inputs (no wheel, no Steervo —
// so it can never leave SAFE), and emits TELEMETRY_V1 frames at 20 Hz.
//
// It exists so the project builds end-to-end and the Pi side can be
// developed against real frames. The legacy bring-up firmware
// (firmware/legacy/kart_controller) remains the image to flash until
// Phase 2 lands real I/O here:
//   TODO(phase-2): Hori wheel via USBHost_t36 -> DriveInputs + pedal maps
//   TODO(phase-2): FlexCAN @ 1 Mbps -> STEER_SET/STEER_STATUS exchange
//   TODO(phase-2): MCP4725 DAC writes + ESC GPIO outputs from DriveOutputs
//   TODO(phase-2): hall pulse speed, hardware watchdog, ARM chord detection

#include <Arduino.h>

#include "drive_state.h"
#include "pedal_map.h"
#include "slew_limiter.h"
#include "telemetry.h"

namespace {

constexpr const char *kVersion = "0.1.0-phase0";

// Pin map (docs/SMCSKart-Mainboard/README.md). Outputs are MOSFET-switched
// ground lines: LOW = released, HIGH = asserted at the ESC.
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

constexpr uint32_t kTickPeriodMs = 10;       // 100 Hz control tick
constexpr uint32_t kTelemetryPeriodMs = 50;  // 20 Hz

kart::DriveStateMachine g_dsm;
kart::SlewLimiter g_throttleSlew(/*rise_per_s=*/25.0f);  // conservative

uint32_t g_lastTickMs = 0;
uint32_t g_lastTelemetryMs = 0;
uint8_t g_telemetrySeq = 0;

String g_usbRx;
String g_piRx;

void applySafePins() {
  digitalWrite(kPinReverse, LOW);
  digitalWrite(kPinBrakeLow, LOW);
  digitalWrite(kPinSpeedHigh, LOW);
  digitalWrite(kPinSpeedLow, LOW);
  digitalWrite(kPinCruise, LOW);
  digitalWrite(kPinContactor, LOW);
  analogWrite(kPinLedRed, 0);
  analogWrite(kPinLedGreen, 0);
  analogWrite(kPinLedBlue, 0);
}

kart::DriveInputs gatherInputs() {
  // Phase 0: no real sensors wired in yet. Everything reads as
  // disconnected/unhealthy, which pins the state machine in SAFE.
  kart::DriveInputs in{};
  in.wheel_connected = false;
  in.steer_link_ok = false;
  in.steer_calibrated = false;
  in.steer_fault = false;
  in.pedal_plausible = true;
  in.dac_ok = true;
  in.throttle_at_zero = true;
  in.vehicle_stopped = true;
  return in;
}

void sendTelemetry(uint32_t now_ms) {
  kart::TelemetryV1 t{};
  t.drive_state = (uint8_t)g_dsm.state();
  t.fault_code = (uint8_t)g_dsm.fault();
  t.status_flags = 0;
  t.throttle_pct = (uint8_t)(g_throttleSlew.value() + 0.5f);
  t.controller_temp_c = kart::kTempUnknown;
  t.motor_temp_c = kart::kTempUnknown;
  t.uptime_ms = now_ms;
  t.seq = g_telemetrySeq++;

  uint8_t frame[kart::kTelemetryV1FrameLen];
  size_t n = kart::encode_telemetry_v1(t, frame, sizeof(frame));
  Serial2.write(frame, n);
}

void handleCommand(const String &line, Stream &out) {
  if (line == "PING") {
    out.println("OK PONG");
  } else if (line == "VERSION") {
    out.print("OK VERSION kart-core ");
    out.print(kVersion);
    out.println(" proto=1");
  } else if (line == "STATUS") {
    out.print("OK STATUS state=");
    out.print((int)g_dsm.state());
    out.print(" fault=");
    out.println((int)g_dsm.fault());
  } else if (line.length() > 0) {
    out.println("ERR UNKNOWN_CMD (phase-0 scaffold: PING|VERSION|STATUS)");
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
  applySafePins();

  Serial.begin(115200);
  Serial2.begin(115200);

  Serial2.println("INFO BOOT kart-core phase-0 scaffold (stays SAFE)");
}

void loop() {
  uint32_t now = millis();

  if ((uint32_t)(now - g_lastTickMs) >= kTickPeriodMs) {
    g_lastTickMs = now;
    g_dsm.tick(gatherInputs(), now);
    kart::DriveOutputs out = g_dsm.outputs();
    // Phase 0: assert only that the safe state stays safe. Real output
    // application lands in Phase 2.
    if (!out.contactor_closed) {
      digitalWrite(kPinContactor, LOW);
    }
  }

  if ((uint32_t)(now - g_lastTelemetryMs) >= kTelemetryPeriodMs) {
    g_lastTelemetryMs = now;
    sendTelemetry(now);
  }

  servicePort(Serial, g_usbRx);
  servicePort(Serial2, g_piRx);
}
