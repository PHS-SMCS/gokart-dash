// Barebones Teensy 4.1 USB-host enumeration probe.
//
// Purpose: settle, with zero other code in the way, whether the Teensy's USB
// host port can see and power the Hori wheel at all. NO CAN, NO I2C, NO drive
// logic — just USBHost_t36 + serial reporting.
//
// How to use:
//   1. Flash this, open the serial monitor on /dev/ttyACM0 @ 115200.
//   2. Watch the heartbeat ("alive, N device(s) enumerated").
//   3. HOT-PLUG the wheel into the Teensy USB-A host port while watching.
//      - A "*** ... CONNECTED vid=.. pid=.." line  => the host works; the wheel
//        enumerates. (Hori = vid 0F0D.)
//      - Nothing ever appears, heartbeat stays at 0 devices => the host port
//        sees no device: it's the 5 V feed / D+/D- wiring / connector (hardware),
//        NOT firmware.
//
// A hot-plug generates a fresh USB connect event, so this does not depend on the
// power-cycle-after-flash quirk that affects the full firmware.

#include <USBHost_t36.h>

USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
USBHIDParser hid1(myusb);
USBHIDParser hid2(myusb);
USBHIDParser hid3(myusb);
JoystickController joystick(myusb);

USBDriver *drivers[] = {&hub1, &hub2, &hid1, &hid2, &hid3, &joystick};
const char *driver_names[] = {"Hub1", "Hub2", "HID1", "HID2", "HID3", "Joystick"};
bool driver_active[] = {false, false, false, false, false, false};
const int kNumDrivers = sizeof(drivers) / sizeof(drivers[0]);

uint32_t g_lastBeat = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
  Serial.println("=== usbhost_probe: barebones Teensy 4.1 USB host test ===");
  Serial.println("Plug the wheel into the Teensy USB-A HOST port and watch.");
  myusb.begin();
}

void loop() {
  myusb.Task();

  for (int i = 0; i < kNumDrivers; i++) {
    if (*drivers[i] != driver_active[i]) {
      driver_active[i] = *drivers[i];
      if (driver_active[i]) {
        Serial.printf("*** %s CONNECTED  vid=%04X pid=%04X\n", driver_names[i],
                      drivers[i]->idVendor(), drivers[i]->idProduct());
        const uint8_t *mfg = drivers[i]->manufacturer();
        const uint8_t *prod = drivers[i]->product();
        if (mfg && *mfg) Serial.printf("      manufacturer: %s\n", mfg);
        if (prod && *prod) Serial.printf("      product:      %s\n", prod);
      } else {
        Serial.printf("*** %s disconnected\n", driver_names[i]);
      }
    }
  }

  // Live axis/button dump so a turn of the wheel is visible immediately.
  if (joystick.available()) {
    Serial.printf("JOY buttons=%08X", (unsigned)joystick.getButtons());
    for (int a = 0; a < 10; a++) Serial.printf(" a%d=%d", a, joystick.getAxis(a));
    Serial.println();
    joystick.joystickDataClear();
  }

  // 2 s heartbeat with a live count of enumerated devices.
  if (millis() - g_lastBeat >= 2000) {
    g_lastBeat = millis();
    int n = 0;
    for (int i = 0; i < kNumDrivers; i++)
      if (driver_active[i]) n++;
    Serial.printf("...alive, %d device(s) enumerated\n", n);
  }
}
