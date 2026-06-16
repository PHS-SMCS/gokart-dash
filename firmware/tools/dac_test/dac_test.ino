// MCP4725 breadboard test — verify a fresh DAC module BEFORE wiring it into
// the kart mainboard. Catches a dead output buffer like the one that failed
// (I2C ACKs fine, but VOUT floats/decays and ignores commands).
//
// Runs on any Arduino-compatible board (Uno/Nano/Teensy/ESP32 — anything with
// Wire). Uses the exact same MCP4725 write the kart firmware uses, so a pass
// here means the chip will work in the kart.
//
// ── Breadboard wiring ──
//   MCP4725 VCC  -> 5V   (or 3.3V — the test prints expected VOUT for both)
//   MCP4725 GND  -> GND  (common with your meter's black lead)
//   MCP4725 SDA  -> board SDA   (Uno/Nano A4 · Teensy 18 · ESP32 21)
//   MCP4725 SCL  -> board SCL   (Uno/Nano A5 · Teensy 19 · ESP32 22)
//   MCP4725 OUT  -> multimeter red lead
//   (most MCP4725 breakouts have onboard I2C pull-ups; if using a bare chip,
//    add ~4.7k from SDA and SCL to VCC.)
//
// ── Use ──
//   Open Serial Monitor @ 115200. On boot it scans for the DAC, then auto-
//   cycles VOUT through 0 / 25 / 50 / 75 / 100 % every 4 s, printing the
//   commanded value, the I2C ACK result, and the expected VOUT at 5 V and
//   3.3 V references.
//
//   PASS: the meter holds STEADY at each step and matches the printed
//         "expect VOUT" value (within a few %). Output is rock-solid, not
//         drifting.
//   FAIL: VOUT floats, decays, or ignores the steps -> dead module, reject it.
//
//   You can also type a value in the Serial Monitor to HOLD it for easy
//   metering:   "50" -> hold 50%   ·   "100" -> full   ·   "c" -> resume cycle

#include <Wire.h>

static uint8_t  g_addr = 0;       // detected DAC address (0x60..0x67)
static int      g_holdPct = -1;   // >=0 = hold this %, -1 = auto-cycle
static uint32_t g_lastStepMs = 0;
static uint8_t  g_step = 0;

bool dacWrite(uint8_t addr, uint16_t raw) {
  if (raw > 4095) raw = 4095;
  Wire.beginTransmission(addr);
  Wire.write(0x40);                          // "Write DAC Register" command
  Wire.write((uint8_t)(raw >> 4));           // D11..D4
  Wire.write((uint8_t)((raw & 0x0F) << 4));  // D3..D0 in high nibble
  return Wire.endTransmission() == 0;
}

uint8_t findDac() {
  for (uint8_t a = 0x60; a <= 0x67; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void report(uint16_t raw) {
  bool ok = dacWrite(g_addr, raw);
  float frac = raw / 4095.0f;
  Serial.print("raw=");      Serial.print(raw);
  Serial.print(" (");        Serial.print(frac * 100.0f, 0); Serial.print("%)  ");
  Serial.print(ok ? "ACK " : "NACK!");
  Serial.print("  expect VOUT ");
  Serial.print(frac * 5.0f, 2);  Serial.print(" V @5V / ");
  Serial.print(frac * 3.3f, 2);  Serial.println(" V @3.3V");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);
  delay(50);
  Serial.println("\n=== MCP4725 breadboard test ===");
  g_addr = findDac();
  if (g_addr) { Serial.print("DAC found at 0x"); Serial.println(g_addr, HEX); }
  else Serial.println("NO DAC on I2C — check VCC/GND/SDA/SCL and pull-ups.");
}

void loop() {
  // Serial input: a number holds that %, 'c' resumes cycling.
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.equalsIgnoreCase("c")) {
      g_holdPct = -1;
      Serial.println("-> resume auto-cycle");
    } else if (line.length()) {
      int p = line.toInt();
      if (p < 0) p = 0; if (p > 100) p = 100;
      g_holdPct = p;
      Serial.print("-> HOLD "); Serial.print(p); Serial.println("%");
      if (g_addr) report((uint16_t)(p / 100.0f * 4095.0f + 0.5f));
    }
  }

  if (!g_addr) {  // keep looking until the DAC appears
    g_addr = findDac();
    if (g_addr) { Serial.print("DAC found at 0x"); Serial.println(g_addr, HEX); }
    delay(1000);
    return;
  }

  if (g_holdPct >= 0) {
    // Re-assert the held value periodically so a brown-out/reset is visible.
    if (millis() - g_lastStepMs >= 1000) {
      g_lastStepMs = millis();
      dacWrite(g_addr, (uint16_t)(g_holdPct / 100.0f * 4095.0f + 0.5f));
    }
    return;
  }

  // Auto-cycle 0 / 25 / 50 / 75 / 100 % every 4 s.
  if (millis() - g_lastStepMs >= 4000) {
    g_lastStepMs = millis();
    static const uint16_t levels[] = {0, 1024, 2048, 3072, 4095};
    report(levels[g_step]);
    g_step = (g_step + 1) % 5;
  }
}
