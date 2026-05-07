#pragma once

#include <Arduino.h>

// -------------------- Serial and bus settings --------------------
static constexpr uint32_t USB_BAUD = 115200;
static constexpr uint32_t PI_BAUD = 115200;
static constexpr uint32_t ESC_BAUD = 115200;
static constexpr uint32_t CAN_BAUD = 500000;

static constexpr uint8_t MCP4725_ADDR = 0x60;

// -------------------- Pin map (from SMCSKart documentation) --------------------
static constexpr uint8_t PIN_HALL_PULSES = 2;   // input from ESC pin 18
static constexpr uint8_t PIN_REVERSE = 3;       // output to ESC REV pin 8
static constexpr uint8_t PIN_BRAKE_LOW = 4;     // output to ESC low brake pin 21
static constexpr uint8_t PIN_SPEED_HIGH = 5;    // output to ESC high speed pin 3
static constexpr uint8_t PIN_SPEED_LOW = 6;     // output to ESC low speed pin 2
static constexpr uint8_t PIN_CRUISE = 9;        // output to ESC cruise/boost
static constexpr uint8_t PIN_PPS = 10;          // input from GPS PPS
static constexpr uint8_t PIN_CONTACTOR = 32;    // output to contactor trigger

// PWM LED channels (24V strip driver)
static constexpr uint8_t PIN_LED_BLUE = 33;
static constexpr uint8_t PIN_LED_GREEN = 36;
static constexpr uint8_t PIN_LED_RED = 37;

// Teensy onboard LED (used as a wheel-input heartbeat during bring-up)
static constexpr uint8_t PIN_ONBOARD_LED = LED_BUILTIN;
static constexpr uint8_t WHEEL_BTN_COUNT = 11;

// -------------------- Throttle DAC --------------------
static constexpr float THROTTLE_V_MIN = 0.5f;
static constexpr float THROTTLE_V_MAX = 4.3f;
static constexpr float THROTTLE_DAC_REF = 5.0f;
static constexpr uint16_t THROTTLE_DAC_MAX = 4095;
