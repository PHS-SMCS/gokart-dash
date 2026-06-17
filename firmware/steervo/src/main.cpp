// Steervo — ESP32 steer-by-wire controller.
//
// Wiring: CAN transceiver CTX->GPIO21, CRX->GPIO22; steering pot wiper on
// GPIO32 (pot powered from 3.3 V); Talon SRX PWM signal in on GPIO25.
//
// The Talon is driven by a standard servo PWM pulse (1.0 ms full reverse,
// 1.5 ms neutral, 2.0 ms full forward) — NOT CTRE CAN frames. So the CAN bus
// carries only the Teensy<->Steervo kart traffic (11-bit standard IDs at
// 1 Mbps); the Talon must be configured for PWM input (no RoboRIO/CAN owner).
// The Talon self-neutralizes if PWM pulses stop (~100 ms), which is the
// hardware failsafe this design leans on; we also command neutral whenever the
// controller is not ACTIVE.
//
// The control core (lib/steervo) is final logic under unit test; this shell
// wires it to TWAI/ADC/PWM/NVS. Motor output stays gated until supervised
// bench bring-up (see kEnableMotorOutput below).

#include <Arduino.h>
#include <Preferences.h>

#include "driver/twai.h"

#include "kart_can.h"
#include "steer_controller.h"

namespace {

constexpr const char *kVersion = "0.2.0-pwm";

// HARD GATE: even a fully ACTIVE controller commands neutral PWM (no motion)
// until this is flipped to true for the supervised bench bring-up.
constexpr bool kEnableMotorOutput = false;

constexpr gpio_num_t kCanTxPin = GPIO_NUM_21;
constexpr gpio_num_t kCanRxPin = GPIO_NUM_22;
constexpr uint8_t kPotPin = 32;
constexpr uint8_t kLedPin = 2;       // onboard LED on most ESP32 dev kits
constexpr uint8_t kTalonPwmPin = 25;  // servo PWM signal to the Talon SRX

// Servo PWM: 50 Hz frame, pulse 1.0–2.0 ms (1.5 ms = neutral). 16-bit duty
// resolution gives ~0.3 µs steps — far finer than the Talon resolves.
constexpr int kPwmFreqHz = 50;
constexpr int kPwmResBits = 16;
constexpr uint32_t kPwmPeriodUs = 1000000u / kPwmFreqHz;  // 20000
constexpr float kPwmNeutralUs = 1500.0f;
constexpr float kPwmSpanUs = 500.0f;  // ±500 µs at demand ±1.0

constexpr uint32_t kControlPeriodMs = 10;   // 100 Hz controller tick
constexpr uint32_t kStatusPeriodMs = 20;    // 50 Hz STEER_STATUS
constexpr uint32_t kTalonCmdPeriodMs = 20;  // 50 Hz PWM update

steervo::SteerController g_ctrl;
Preferences g_prefs;

uint32_t g_lastControlMs = 0;
uint32_t g_lastStatusMs = 0;
uint32_t g_lastTalonCmdMs = 0;
// (PWM updates only; no CTRE enable stream — the Talon's PWM-loss failsafe and
// our neutral-when-inactive command replace the periodic CTRE global enable.)
float g_demand = 0.0f;
uint32_t g_txFailures = 0;

// Drive the Talon PWM pin from a motor demand fraction in [-1, 1].
// demand 0 -> neutral (1.5 ms); the sign maps to motor direction.
void writeTalonPwm(float demand) {
  if (demand > 1.0f) demand = 1.0f;
  if (demand < -1.0f) demand = -1.0f;
  float pulse_us = kPwmNeutralUs + demand * kPwmSpanUs;
  uint32_t max_duty = (1u << kPwmResBits) - 1u;
  uint32_t duty = (uint32_t)((pulse_us / (float)kPwmPeriodUs) * max_duty + 0.5f);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(kTalonPwmPin, duty);
#else
  ledcWrite(0 /*channel*/, duty);
#endif
}

bool startTwai() {
  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT(kCanTxPin, kCanRxPin, TWAI_MODE_NORMAL);
  g.rx_queue_len = 32;
  g.tx_queue_len = 16;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    return false;
  }
  return twai_start() == ESP_OK;
}

void loadCalibration() {
  steervo::PotCalibration cal{};
  g_prefs.begin("steervo", /*readOnly=*/false);
  cal.valid = g_prefs.getBool("cal_valid", false);
  if (cal.valid) {
    cal.raw_center = g_prefs.getUShort("cal_center", 2048);
    cal.raw_left = g_prefs.getUShort("cal_left", 1024);
    cal.raw_right = g_prefs.getUShort("cal_right", 3072);
    cal.angle_left_cdeg = (int16_t)g_prefs.getShort("cal_a_left", -3000);
    cal.angle_right_cdeg = (int16_t)g_prefs.getShort("cal_a_right", 3000);
  }
  g_ctrl.set_calibration(cal);
  Serial.printf("INFO CAL %s\n", cal.valid ? "loaded" : "none (NOT_CALIBRATED)");
}

void sendKartFrame(uint32_t id, const uint8_t *data, uint8_t dlc) {
  twai_message_t tx = {};
  tx.identifier = id;
  tx.extd = 0;  // kart traffic is 11-bit standard
  tx.data_length_code = dlc;
  memcpy(tx.data, data, dlc);
  if (twai_transmit(&tx, pdMS_TO_TICKS(5)) != ESP_OK) {
    g_txFailures++;
  }
}

void persistCalibration() {
  const steervo::PotCalibration &c = g_ctrl.calibration();
  g_prefs.putBool("cal_valid", c.valid);
  g_prefs.putUShort("cal_center", c.raw_center);
  g_prefs.putUShort("cal_left", c.raw_left);
  g_prefs.putUShort("cal_right", c.raw_right);
  g_prefs.putShort("cal_a_left", c.angle_left_cdeg);
  g_prefs.putShort("cal_a_right", c.angle_right_cdeg);
  Serial.printf("INFO CAL saved center=%u left=%u right=%u\n", c.raw_center,
                c.raw_left, c.raw_right);
}

void serviceCanRx(uint32_t now_ms) {
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) {
    if (rx.rtr || rx.extd) {
      continue;  // CTRE chatter and remote frames are not for us
    }
    switch (rx.identifier) {
      case kart::kIdSteerSet: {
        kart::SteerSet msg;
        if (kart::unpack_steer_set(rx.data, rx.data_length_code, msg)) {
          g_ctrl.on_steer_set(msg, now_ms);
        }
        break;
      }
      case kart::kIdSteerCal: {
        kart::SteerCalCmd cmd;
        if (kart::unpack_steer_cal(rx.data, rx.data_length_code, cmd)) {
          uint16_t pot = (uint16_t)analogRead(kPotPin);
          if (g_ctrl.on_cal(cmd, pot)) {
            persistCalibration();
          } else {
            Serial.printf("INFO CAL cmd=%d pot=%u state=%d\n", (int)cmd, pot,
                          (int)g_ctrl.state());
          }
        }
        break;
      }
      case kart::kIdSteerCfg: {
        kart::SteerCfg msg;
        if (kart::unpack_steer_cfg(rx.data, rx.data_length_code, msg) &&
            g_ctrl.state() != kart::SteerState::kActive) {
          g_ctrl.on_cfg(msg);
        }
        break;
      }
      default:
        break;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(kLedPin, OUTPUT);
  analogReadResolution(12);

  // Servo PWM to the Talon; start at neutral so the motor is parked.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(kTalonPwmPin, kPwmFreqHz, kPwmResBits);
#else
  ledcSetup(0 /*channel*/, kPwmFreqHz, kPwmResBits);
  ledcAttachPin(kTalonPwmPin, 0 /*channel*/);
#endif
  writeTalonPwm(0.0f);

  Serial.printf("INFO BOOT steervo %s (Talon PWM on GPIO%u, motor output %s)\n",
                kVersion, kTalonPwmPin,
                kEnableMotorOutput ? "ENABLED" : "disabled");

  if (!startTwai()) {
    Serial.println("ERR TWAI init failed; halting");
    while (true) {
      digitalWrite(kLedPin, !digitalRead(kLedPin));
      delay(120);
    }
  }

  loadCalibration();
  Serial.println("INFO steervo ready (CAN 1M)");
}

void loop() {
  uint32_t now = millis();

  serviceCanRx(now);

  if ((uint32_t)(now - g_lastControlMs) >= kControlPeriodMs) {
    g_lastControlMs = now;
    uint16_t pot = (uint16_t)analogRead(kPotPin);
    g_demand = g_ctrl.tick(now, pot);
    // Heartbeat LED: solid in ACTIVE, off otherwise.
    digitalWrite(kLedPin,
                 g_ctrl.state() == kart::SteerState::kActive ? HIGH : LOW);
  }

  // Talon servo PWM: command the demand only while the controller is genuinely
  // ACTIVE and the motor gate is set; otherwise hold neutral (0). If the loop
  // ever stops refreshing the pin, the Talon self-neutralizes within ~100 ms.
  bool motor_live = kEnableMotorOutput &&
                    g_ctrl.state() == kart::SteerState::kActive;
  if ((uint32_t)(now - g_lastTalonCmdMs) >= kTalonCmdPeriodMs) {
    g_lastTalonCmdMs = now;
    writeTalonPwm(motor_live ? g_demand : 0.0f);
  }

  if ((uint32_t)(now - g_lastStatusMs) >= kStatusPeriodMs) {
    g_lastStatusMs = now;
    kart::SteerStatus st = g_ctrl.status();
    uint8_t buf[kart::kSteerStatusDlc];
    kart::pack_steer_status(st, buf);
    sendKartFrame(kart::kIdSteerStatus, buf, sizeof(buf));
  }
}
