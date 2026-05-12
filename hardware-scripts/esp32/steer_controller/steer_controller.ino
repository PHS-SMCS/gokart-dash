// SMCSKart steering controller (ESP32 over CAN).
//
// Connects to the Teensy 4.1 mainboard via the CAN bus through a 3.3 V CAN
// transceiver (e.g. SN65HVD230 / TJA1051T/3). Reply this firmware exchanges a
// "hello" handshake; steering control logic will be layered on top of the same
// CAN ID pair.
//
// Protocol (see kart_controller.ino):
//   0x100  Teensy -> ESP32   payload: 0x01 'H' 'E' 'L' 'O'
//   0x101  ESP32  -> Teensy  payload: 0x81 'H' 'I' <fw_major> <fw_minor>
//
// Bitrate: 500 kbps, 11-bit standard IDs. 120 Ohm termination at each end.
//
// Build: Arduino-ESP32 core. Tested target = ESP32 (classic). For S2/S3/C3
// adjust the TWAI pin macros to match available GPIOs and rebuild.

#include <Arduino.h>
#include "driver/twai.h"

// Wire CAN_TX / CAN_RX on the ESP32 to the TXD / RXD pins of the transceiver.
// Any GPIO that can be muxed to the TWAI peripheral works; these are the
// defaults used on most ESP32 dev kits.
static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_5;
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_4;

static constexpr uint32_t CAN_ID_STEER_HELLO_REQ = 0x100;
static constexpr uint32_t CAN_ID_STEER_HELLO_ACK = 0x101;
static constexpr uint8_t STEER_MSG_HELLO_REQ = 0x01;
static constexpr uint8_t STEER_MSG_HELLO_ACK = 0x81;

static constexpr uint8_t FW_MAJOR = 0;
static constexpr uint8_t FW_MINOR = 1;

static bool g_twaiUp = false;

static bool startTwai() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  g.rx_queue_len = 16;
  g.tx_queue_len = 8;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t r = twai_driver_install(&g, &t, &f);
  if (r != ESP_OK) {
    Serial.printf("ERR twai_driver_install: %d\n", r);
    return false;
  }
  r = twai_start();
  if (r != ESP_OK) {
    Serial.printf("ERR twai_start: %d\n", r);
    twai_driver_uninstall();
    return false;
  }
  return true;
}

static void sendHelloAck() {
  twai_message_t tx = {};
  tx.identifier = CAN_ID_STEER_HELLO_ACK;
  tx.data_length_code = 5;
  tx.data[0] = STEER_MSG_HELLO_ACK;
  tx.data[1] = 'H';
  tx.data[2] = 'I';
  tx.data[3] = FW_MAJOR;
  tx.data[4] = FW_MINOR;

  esp_err_t r = twai_transmit(&tx, pdMS_TO_TICKS(100));
  if (r != ESP_OK) {
    Serial.printf("ERR ack tx: %d\n", r);
    return;
  }
  Serial.println("ACK sent");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.printf("steer_controller booting (fw %u.%u)\n", FW_MAJOR, FW_MINOR);

  g_twaiUp = startTwai();
  if (!g_twaiUp) {
    Serial.println("ERR TWAI init failed; halting");
    while (true) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(120);
      digitalWrite(LED_BUILTIN, LOW);
      delay(120);
    }
  }
  Serial.println("steer_controller ready (CAN 500k)");
}

void loop() {
  twai_message_t rx;
  esp_err_t r = twai_receive(&rx, pdMS_TO_TICKS(100));
  if (r == ESP_ERR_TIMEOUT) {
    return;
  }
  if (r != ESP_OK) {
    Serial.printf("ERR twai_receive: %d\n", r);
    return;
  }

  if (rx.flags & TWAI_MSG_FLAG_RTR) {
    return;
  }

  if (rx.identifier == CAN_ID_STEER_HELLO_REQ &&
      rx.data_length_code >= 1 &&
      rx.data[0] == STEER_MSG_HELLO_REQ) {
    Serial.println("HELLO recv");
    digitalWrite(LED_BUILTIN, HIGH);
    sendHelloAck();
    digitalWrite(LED_BUILTIN, LOW);
  }
}
