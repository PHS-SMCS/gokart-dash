#pragma once

#include <Arduino.h>

void setGroundSwitchPin(uint8_t pin, bool asserted);
void writeLedHardware(uint8_t r, uint8_t g, uint8_t b);

void writeThrottleRaw(uint16_t raw);
uint16_t setThrottlePercent(float percent);

void setReverse(bool on);
void setBrake(bool on);
void setCruise(bool on);
void setContactor(bool on);
void setSpeedLow(bool on);
void setSpeedHigh(bool on);
void applySpeedMode(const char *mode);
