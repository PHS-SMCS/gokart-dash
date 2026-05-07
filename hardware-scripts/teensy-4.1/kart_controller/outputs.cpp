#include "outputs.h"

#include <Wire.h>

#include "pins.h"
#include "state.h"

void setGroundSwitchPin(uint8_t pin, bool asserted) {
  // Hardware path is MOSFET-switched ground. Command semantics use asserted=true
  // to mean "ground/activate ESC input".
  digitalWrite(pin, asserted ? HIGH : LOW);
}

void writeLedHardware(uint8_t r, uint8_t g, uint8_t b) {
  g_ledR = r;
  g_ledG = g;
  g_ledB = b;
  analogWrite(PIN_LED_RED, r);
  analogWrite(PIN_LED_GREEN, g);
  analogWrite(PIN_LED_BLUE, b);
}

void writeThrottleRaw(uint16_t raw) {
  raw = raw > THROTTLE_DAC_MAX ? THROTTLE_DAC_MAX : raw;

  Wire.beginTransmission(MCP4725_ADDR);
  Wire.write(0x40);                         // Fast mode write DAC register
  Wire.write((uint8_t)(raw >> 4));          // D11..D4
  Wire.write((uint8_t)((raw & 0x0F) << 4)); // D3..D0 xxxx
  Wire.endTransmission();
}

uint16_t setThrottlePercent(float percent) {
  if (percent < 0.0f) {
    percent = 0.0f;
  }
  if (percent > 100.0f) {
    percent = 100.0f;
  }

  g_throttlePct = percent;

  float v = THROTTLE_V_MIN + ((THROTTLE_V_MAX - THROTTLE_V_MIN) * (percent / 100.0f));
  float normalized = v / THROTTLE_DAC_REF;
  if (normalized < 0.0f) {
    normalized = 0.0f;
  }
  if (normalized > 1.0f) {
    normalized = 1.0f;
  }

  uint16_t raw = (uint16_t)roundf(normalized * THROTTLE_DAC_MAX);
  writeThrottleRaw(raw);
  return raw;
}

void setReverse(bool on) {
  g_reverseOn = on;
  setGroundSwitchPin(PIN_REVERSE, on);
}

void setBrake(bool on) {
  g_brakeOn = on;
  setGroundSwitchPin(PIN_BRAKE_LOW, on);
}

void setCruise(bool on) {
  g_cruiseOn = on;
  setGroundSwitchPin(PIN_CRUISE, on);
}

void setContactor(bool on) {
  g_contactorOn = on;
  setGroundSwitchPin(PIN_CONTACTOR, on);
}

void setSpeedLow(bool on) {
  g_speedLowOn = on;
  setGroundSwitchPin(PIN_SPEED_LOW, on);
}

void setSpeedHigh(bool on) {
  g_speedHighOn = on;
  setGroundSwitchPin(PIN_SPEED_HIGH, on);
}

void applySpeedMode(const char *mode) {
  if (strcmp(mode, "low") == 0) {
    setSpeedHigh(false);
    setSpeedLow(true);
  } else if (strcmp(mode, "high") == 0) {
    setSpeedLow(false);
    setSpeedHigh(true);
  } else {
    // medium/default => both deasserted
    setSpeedLow(false);
    setSpeedHigh(false);
  }
}
