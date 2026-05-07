#pragma once

#include <Arduino.h>
#include <FlexCAN_T4.h>

// Hall pulse counter — modified from ISR.
extern volatile uint32_t g_hallPulseCount;

// Arm-window state (millis() timestamp; 0 = disarmed).
extern uint32_t g_armUntilMs;
extern bool g_wasArmedLastLoop;

// ESC output state (hardware mirror).
extern bool g_reverseOn;
extern bool g_brakeOn;
extern bool g_speedLowOn;
extern bool g_speedHighOn;
extern bool g_cruiseOn;
extern bool g_contactorOn;
extern float g_throttlePct;

// LED hardware state (the bytes currently being driven on PWM pins).
extern uint8_t g_ledR;
extern uint8_t g_ledG;
extern uint8_t g_ledB;

// LED "manual" state — the color set by LED command, applied when no wheel
// button is held.
extern uint8_t g_ledManualR;
extern uint8_t g_ledManualG;
extern uint8_t g_ledManualB;

// Per-port line buffers used by servicePort().
extern String g_usbRx;
extern String g_piRx;

// CAN bus instance.
extern FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
