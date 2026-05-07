#pragma once

#include <Arduino.h>

// Wheel button mask managed by Pi-side WHEEL_BTN commands.
extern uint16_t g_wheelBtnMask;

// USB host wheel state (mirror of g_joystick).
extern bool g_wheelHostConnected;
extern uint32_t g_wheelHostButtons;

uint16_t combinedWheelButtons();

// Set the "manual" (non-wheel-driven) LED color. Wheel input takes precedence
// while any button is held; the manual color is restored on release.
void setLed(uint8_t r, uint8_t g, uint8_t b);

void refreshLedFromWheel();
void updateOnboardLedFromWheel();

// Drive the USBHost stack and forward button changes.
void serviceUsbHostWheel();

// Initialize USBHost (called from setup()).
void wheelBegin();

// Read identity of attached USB joystick (for the WHEEL? command).
uint16_t wheelJoystickVid();
uint16_t wheelJoystickPid();
int wheelJoystickType();
