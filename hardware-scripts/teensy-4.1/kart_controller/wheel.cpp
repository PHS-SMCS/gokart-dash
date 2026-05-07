#include "wheel.h"

#include <USBHost_t36.h>

#include "pins.h"
#include "outputs.h"
#include "state.h"

// Temporary forward declaration; provided by parser.h after Task 7.
void broadcastInfo(const char *msg);

uint16_t g_wheelBtnMask = 0;

bool g_wheelHostConnected = false;
uint32_t g_wheelHostButtons = 0;

namespace {

struct WheelLedColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr WheelLedColor kWheelButtonColors[WHEEL_BTN_COUNT] = {
  {0,   255, 0  }, // 0  A    -> green
  {255, 0,   0  }, // 1  B    -> red
  {0,   0,   255}, // 2  X    -> blue
  {255, 255, 0  }, // 3  Y    -> yellow
  {0,   255, 255}, // 4  LB   -> cyan
  {255, 0,   255}, // 5  RB   -> magenta
  {255, 255, 255}, // 6  View -> white
  {255, 96,  0  }, // 7  Menu -> orange
  {128, 0,   255}, // 8  Xbox -> purple
  {64,  64,  64 }, // 9  LSB  -> dim white
  {255, 32,  96 }, // 10 RSB  -> pink
};

USBHost g_usbHost;
USBHub g_usbHub1(g_usbHost);
USBHub g_usbHub2(g_usbHost);
JoystickController g_joystick(g_usbHost);

}  // namespace

uint16_t combinedWheelButtons() {
  return g_wheelBtnMask | (uint16_t)(g_wheelHostButtons & 0xFFFF);
}

void refreshLedFromWheel() {
  uint16_t mask = combinedWheelButtons();
  if (mask == 0) {
    writeLedHardware(g_ledManualR, g_ledManualG, g_ledManualB);
    return;
  }

  // Blend the colors of every held button (saturating add) so chords mix.
  uint16_t r = 0, g = 0, b = 0;
  for (uint8_t i = 0; i < WHEEL_BTN_COUNT; i++) {
    if (mask & ((uint16_t)1 << i)) {
      r += kWheelButtonColors[i].r;
      g += kWheelButtonColors[i].g;
      b += kWheelButtonColors[i].b;
    }
  }
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  writeLedHardware((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

void updateOnboardLedFromWheel() {
  digitalWrite(PIN_ONBOARD_LED, combinedWheelButtons() != 0 ? HIGH : LOW);
  refreshLedFromWheel();
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  // LED command sets the "manual" color; wheel input takes precedence while any
  // button is held, and the manual color is restored on release.
  g_ledManualR = r;
  g_ledManualG = g;
  g_ledManualB = b;
  refreshLedFromWheel();
}

void serviceUsbHostWheel() {
  g_usbHost.Task();

  bool connectedNow = (bool)g_joystick;
  if (connectedNow != g_wheelHostConnected) {
    g_wheelHostConnected = connectedNow;
    if (connectedNow) {
      char msg[80];
      snprintf(msg, sizeof(msg),
               "INFO WHEEL_HOST_CONNECTED vid=0x%04X pid=0x%04X type=%d",
               g_joystick.idVendor(), g_joystick.idProduct(),
               (int)g_joystick.joystickType());
      broadcastInfo(msg);
    } else {
      broadcastInfo("INFO WHEEL_HOST_DISCONNECTED");
      g_wheelHostButtons = 0;
      updateOnboardLedFromWheel();
    }
  }

  if (!connectedNow) {
    return;
  }

  if (g_joystick.available()) {
    g_wheelHostButtons = g_joystick.getButtons();
    updateOnboardLedFromWheel();
    g_joystick.joystickDataClear();
  }
}

void wheelBegin() {
  g_usbHost.begin();
}

uint16_t wheelJoystickVid() { return g_joystick.idVendor(); }
uint16_t wheelJoystickPid() { return g_joystick.idProduct(); }
int wheelJoystickType()     { return (int)g_joystick.joystickType(); }
