#include <Arduino.h>
#include <Wire.h>
#include <FlexCAN_T4.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pins.h"
#include "state.h"
#include "outputs.h"
#include "wheel.h"
#include "safety.h"
#include "parser.h"
#include "commands.h"

void setup() {
  pinMode(PIN_HALL_PULSES, INPUT_PULLUP);
  pinMode(PIN_PPS, INPUT);

  pinMode(PIN_REVERSE, OUTPUT);
  pinMode(PIN_BRAKE_LOW, OUTPUT);
  pinMode(PIN_SPEED_HIGH, OUTPUT);
  pinMode(PIN_SPEED_LOW, OUTPUT);
  pinMode(PIN_CRUISE, OUTPUT);
  pinMode(PIN_CONTACTOR, OUTPUT);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  digitalWrite(PIN_ONBOARD_LED, LOW);

  analogWriteResolution(8);

  // Ensure deterministic safe defaults immediately
  digitalWrite(PIN_REVERSE, LOW);
  digitalWrite(PIN_BRAKE_LOW, LOW);
  digitalWrite(PIN_SPEED_HIGH, LOW);
  digitalWrite(PIN_SPEED_LOW, LOW);
  digitalWrite(PIN_CRUISE, LOW);
  digitalWrite(PIN_CONTACTOR, LOW);
  analogWrite(PIN_LED_RED, 0);
  analogWrite(PIN_LED_GREEN, 0);
  analogWrite(PIN_LED_BLUE, 0);

  Serial.begin(USB_BAUD);
  Serial1.begin(ESC_BAUD);
  Serial2.begin(PI_BAUD);

  Wire.begin();

  Can0.begin();
  Can0.setBaudRate(CAN_BAUD);

  wheelBegin();

  attachInterrupt(digitalPinToInterrupt(PIN_HALL_PULSES), onHallPulse, RISING);

  applySafeState();
  g_armUntilMs = 0;
  g_wasArmedLastLoop = false;

  delay(20);
  broadcastInfo("OK BOOT kart_controller ready");
}

void loop() {
  servicePort(Serial, g_usbRx);
  servicePort(Serial2, g_piRx);
  serviceUsbHostWheel();
  serviceArmTimeout();
}
