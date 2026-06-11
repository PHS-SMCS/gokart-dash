// Steervo — ESP32 steer-by-wire controller (Phase 0 scaffold).
//
// Wiring (docs: "Wiring Doc for Steervo"): CAN transceiver CTX->GPIO21,
// CRX->GPIO22; steering pot wiper on GPIO32 (pot powered from 3.3 V).
// The Talon SRX and the Teensy share this single 1 Mbps bus: CTRE frames
// use 29-bit extended IDs, kart frames use 11-bit standard IDs.
//
// The control core (lib/steervo) is final logic under unit test; this shell
// wires it to TWAI/ADC/NVS. Motor output remains disabled until Phase 1
// bench bring-up (see kEnableMotorOutput below).

#include <Arduino.h>
#include <Preferences.h>

#include "driver/twai.h"

#include "ctre_frames.h"
#include "kart_can.h"
#include "steer_controller.h"

namespace {

constexpr const char *kVersion = "0.1.0-phase0";

// HARD GATE for Phase 0: even a fully ACTIVE controller sends zero demand to
// the Talon. Flipped to true only for the supervised Phase 1 bench test.
constexpr bool kEnableMotorOutput = false;

constexpr gpio_num_t kCanTxPin = GPIO_NUM_21;
constexpr gpio_num_t kCanRxPin = GPIO_NUM_22;
constexpr uint8_t kPotPin = 32;
constexpr uint8_t kLedPin = 2;  // onboard LED on most ESP32 dev kits
constexpr uint8_t kTalonDeviceNumber = 0;  // TODO(phase-1): confirm on bench

constexpr uint32_t kControlPeriodMs = 10;   // 100 Hz controller tick
constexpr uint32_t kStatusPeriodMs = 20;    // 50 Hz STEER_STATUS
constexpr uint32_t kEnablePeriodMs = 50;    // 20 Hz CTRE global enable
constexpr uint32_t kTalonCmdPeriodMs = 20;  // 50 Hz percent output

steervo::SteerController g_ctrl;
Preferences g_prefs;

uint32_t g_lastControlMs = 0;
uint32_t g_lastStatusMs = 0;
uint32_t g_lastEnableMs = 0;
uint32_t g_lastTalonCmdMs = 0;
float g_demand = 0.0f;
uint32_t g_txFailures = 0;

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

void sendCtreFrame(const steervo::CanFrame &f) {
  twai_message_t tx = {};
  tx.identifier = f.id;
  tx.extd = 1;  // CTRE traffic is 29-bit extended
  tx.data_length_code = f.dlc;
  memcpy(tx.data, f.data, f.dlc);
  if (twai_transmit(&tx, pdMS_TO_TICKS(5)) != ESP_OK) {
    g_txFailures++;
    g_ctrl.set_talon_lost(true);
  } else {
    g_ctrl.set_talon_lost(false);
  }
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
          // TODO(phase-1): guided calibration sequence + NVS persistence.
          Serial.printf("INFO CAL cmd=%d (not implemented in phase-0)\n",
                        (int)cmd);
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

  Serial.printf("INFO BOOT steervo %s (motor output %s)\n", kVersion,
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

  // CTRE global enable: emitted only while we genuinely want the motor live.
  // If this stream stops for any reason (crash, brownout), the Talon
  // self-disables within ~100 ms.
  bool motor_live = kEnableMotorOutput &&
                    g_ctrl.state() == kart::SteerState::kActive;
  if ((uint32_t)(now - g_lastEnableMs) >= kEnablePeriodMs) {
    g_lastEnableMs = now;
    sendCtreFrame(steervo::ctre_global_enable(motor_live));
  }
  if ((uint32_t)(now - g_lastTalonCmdMs) >= kTalonCmdPeriodMs) {
    g_lastTalonCmdMs = now;
    float demand = motor_live ? g_demand : 0.0f;
    sendCtreFrame(steervo::talon_percent_output(kTalonDeviceNumber, demand));
  }

  if ((uint32_t)(now - g_lastStatusMs) >= kStatusPeriodMs) {
    g_lastStatusMs = now;
    kart::SteerStatus st = g_ctrl.status();
    uint8_t buf[kart::kSteerStatusDlc];
    kart::pack_steer_status(st, buf);
    sendKartFrame(kart::kIdSteerStatus, buf, sizeof(buf));
  }
}
