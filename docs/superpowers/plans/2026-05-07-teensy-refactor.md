# Teensy Firmware Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the 925-line `kart_controller.ino` into 7 focused modules within the same Arduino sketch folder (using the standard `.h`/`.cpp` multi-file pattern), and replace the 374-line monolithic `handleCommand` `if/else` cascade with a command-table dispatch — without changing any byte-level firmware behavior.

**Architecture:** Each module owns one responsibility. `state.{h,cpp}` holds shared globals (the `g_*` variables); other modules `extern`-declare what they need via `state.h`. `pins.h` is constants-only. `outputs`, `wheel`, `safety`, `parser`, `commands` each pair a `.h` (declarations + types) with a `.cpp` (definitions). The entry-point `kart_controller.ino` shrinks to includes + `setup()` + `loop()` + service-call orchestration.

**Tech Stack:** C++ (Arduino Teensyduino dialect, GCC ARM 11.3.1), Teensy core 1.60.0, FlexCAN_T4 + USBHost_t36 + Wire libraries (bundled with Teensy core). Build via `arduino-cli compile --fqbn teensy:avr:teensy41`.

**Reference:** Spec at [`docs/superpowers/specs/2026-05-07-teensy-refactor-design.md`](../specs/2026-05-07-teensy-refactor-design.md).

**Verification approach:** Every commit must produce a clean `arduino-cli compile` and a memory footprint within ±5% of the captured baseline (FLASH 81 KB code + 12 KB data, RAM1 31 KB variables + 79 KB code). Per-command response text is reviewed against the pre-refactor `OK`/`ERR` outputs in the final dispatch-refactor task. Hardware smoke (flash + run `kartctl ping`/`status`/`validate bringup`) is deferred to Pi deploy-test.

---

## Pre-flight check

This plan assumes:
- `arduino-cli` is on PATH (full path: `C:\Program Files\GitHub CLI\gh.exe` for `gh`; `arduino-cli` is in the user PATH after `winget install ArduinoSA.CLI`).
- The `teensy:avr@1.60.0` core is installed (`arduino-cli core install teensy:avr`).
- The pre-refactor `kart_controller.ino` already compiles (verified at the start of this branch).

If any of those fail at Task 1, STOP and report — the plan can't proceed without the build gate.

---

## File Structure

**Create (13 new files in `hardware-scripts/teensy-4.1/kart_controller/`):**

| File | Owns |
|---|---|
| `pins.h` | All `PIN_*` constants, bus baud rates, `MCP4725_ADDR`, `WHEEL_BTN_COUNT`, throttle DAC limits |
| `state.h` | `extern` declarations for shared `g_*` globals (hall counter, arm timestamp, output booleans, throttle %, LED bytes, line buffers, FlexCAN instance) |
| `state.cpp` | Definitions of those globals |
| `outputs.h` | `setReverse/setBrake/setSpeed{Low,High}/setCruise/setContactor`, `setGroundSwitchPin`, `writeThrottleRaw`, `setThrottlePercent`, `applySpeedMode`, `writeLedHardware` |
| `outputs.cpp` | Definitions |
| `wheel.h` | `setLed`, `serviceUsbHostWheel`, `combinedWheelButtons`, `WheelLedColor` type, plus `extern bool g_wheelHostConnected` and `extern uint32_t g_wheelHostButtons` |
| `wheel.cpp` | Per-button color table, `kWheelButtonColors[]`, USBHost/Hub/JoystickController instances, `refreshLedFromWheel`, `updateOnboardLedFromWheel`, `setLed`, `serviceUsbHostWheel` |
| `safety.h` | `isArmed`, `armRemainingMs`, `applySafeState`, `serviceArmTimeout` |
| `safety.cpp` | Definitions |
| `parser.h` | `parseOnOff`, `parseFloatStrict`, `parseUInt32Strict`, `hexNibble`, `parseHexBytes`, `printHexByte`, `requireArmed`, `broadcastInfo`, `servicePort`, `onHallPulse` |
| `parser.cpp` | Definitions |
| `commands.h` | `handleCommand`, `printHelp`, `printStatus`, `applyNamedOutput`, `CommandHandler` struct, `kCommands[]` table |
| `commands.cpp` | All per-command handlers + table + dispatcher |

**Modify:**
- `hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino` — shrinks to ~80 lines: includes + `setup()` + `loop()`.

**Delete:** None.

---

## Task 1: Capture pre-refactor baseline + verify toolchain

The compile + memory baseline is the merge gate's reference point. Capture it once, save the values, refer to them in every subsequent task.

**Files:**
- Create: `docs/superpowers/specs/teensy-refactor-baseline.txt` (deleted at end of Task 10)

- [ ] **Step 1: Verify toolchain is reachable**

```bash
arduino-cli version
```

Expected: prints `arduino-cli  Version: 1.x.x ...`. If "command not found", STOP — the user needs to install or fix PATH.

```bash
arduino-cli core list 2>&1 | grep -i teensy
```

Expected: a line like `teensy:avr  1.60.0  ...  Teensy (for Arduino IDE 2.0.4 or later)`. If missing, run `arduino-cli core install teensy:avr` and verify again.

- [ ] **Step 2: Compile pre-refactor sketch and capture memory output**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller > docs/superpowers/specs/teensy-refactor-baseline.txt 2>&1
```

Expected: exit 0. The captured file contains lines like:

```
Memory Usage on Teensy 4.1:
  FLASH: code:81928, data:12472, headers:9020   free for files:8023044
   RAM1: variables:31040, code:79016, padding:19288   free for local variables:394944
   RAM2: variables:12416  free for malloc/new:511872
```

If exit non-zero: the pre-refactor build is broken; STOP.

- [ ] **Step 3: Display the baseline values**

```bash
cat docs/superpowers/specs/teensy-refactor-baseline.txt
```

Confirm the output looks like the example above. Note the four numbers we'll watch:
- FLASH `code:` (≈81928)
- FLASH `data:` (≈12472)
- RAM1 `variables:` (≈31040)
- RAM2 `variables:` (≈12416)

These are the parity check targets. Each subsequent task's compile output should land within ±5% of these.

- [ ] **Step 4: Commit the baseline capture**

```bash
git add docs/superpowers/specs/teensy-refactor-baseline.txt
git commit -m "test(teensy): capture pre-refactor compile baseline

Baseline memory footprint is the merge gate for this refactor:
post-refactor compile must land within +/-5% of these numbers.
File deleted at the end of the migration.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Extract `pins.h`

Move all pin/bus/limit constants out of `kart_controller.ino` into a header. The constants are pure declarations — no functions, no globals — so this task has no link-time risk.

**Files:**
- Create: `hardware-scripts/teensy-4.1/kart_controller/pins.h`
- Modify: `hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino` (delete the moved constants, add `#include "pins.h"`)

- [ ] **Step 1: Create `pins.h`**

Write `hardware-scripts/teensy-4.1/kart_controller/pins.h`:

```cpp
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
```

Note: `static constexpr` at file scope in a header gives each translation unit its own copy. For `uint8_t` constants this costs literally zero bytes (the compiler folds them as immediates). The behavior is identical to the existing inline constants.

- [ ] **Step 2: Update `kart_controller.ino`**

In `hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino`:

After line 8 (after the `#include <string.h>` line), add:

```cpp
#include "pins.h"
```

Then **delete lines 10–41** of the original file (the `// -------------------- Serial and bus settings --------------------` block through the `static constexpr uint16_t THROTTLE_DAC_MAX = 4095;` line — all the constants now in `pins.h`).

After this edit, lines 1-9 of the file should be:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <FlexCAN_T4.h>
#include <USBHost_t36.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pins.h"
```

…and the next line jumps directly to `volatile uint32_t g_hallPulseCount = 0;` (which was line 43 of the original).

- [ ] **Step 3: Compile and verify memory parity**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory output should match the baseline byte-for-byte (constants don't affect linkage). If FLASH `code:` differs by more than a handful of bytes, STOP and investigate — the constants got duplicated somewhere.

- [ ] **Step 4: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/pins.h hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino
git commit -m "refactor(teensy): extract pins.h with all PIN_* and bus constants

Pure declaration move — no link-time changes. arduino-cli compile gate
passes, memory footprint identical to baseline.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Extract `state.{h,cpp}` for shared globals

Move the `g_*` globals (hall counter, arm timestamp, output flags, throttle, LED bytes, RX line buffers, FlexCAN instance) into a dedicated module. Wheel-specific globals (USBHost objects, `g_wheelHostConnected`, `g_wheelHostButtons`, `g_wheelBtnMask`) stay in `kart_controller.ino` for now — they move out in Task 5 with their consumers.

**Files:**
- Create: `state.h`, `state.cpp`
- Modify: `kart_controller.ino`

- [ ] **Step 1: Create `state.h`**

```cpp
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
```

- [ ] **Step 2: Create `state.cpp`**

```cpp
#include "state.h"

volatile uint32_t g_hallPulseCount = 0;

uint32_t g_armUntilMs = 0;
bool g_wasArmedLastLoop = false;

bool g_reverseOn = false;
bool g_brakeOn = false;
bool g_speedLowOn = false;
bool g_speedHighOn = false;
bool g_cruiseOn = false;
bool g_contactorOn = false;
float g_throttlePct = 0.0f;

uint8_t g_ledR = 0;
uint8_t g_ledG = 0;
uint8_t g_ledB = 0;
uint8_t g_ledManualR = 0;
uint8_t g_ledManualG = 0;
uint8_t g_ledManualB = 0;

String g_usbRx;
String g_piRx;

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
```

- [ ] **Step 3: Update `kart_controller.ino`**

Add `#include "state.h"` after `#include "pins.h"`.

Then DELETE the following definitions from the .ino (originally around lines 43–87):

```cpp
volatile uint32_t g_hallPulseCount = 0;

uint32_t g_armUntilMs = 0;
bool g_wasArmedLastLoop = false;

bool g_reverseOn = false;
bool g_brakeOn = false;
bool g_speedLowOn = false;
bool g_speedHighOn = false;
bool g_cruiseOn = false;
bool g_contactorOn = false;
float g_throttlePct = 0.0f;
uint8_t g_ledR = 0;
uint8_t g_ledG = 0;
uint8_t g_ledB = 0;
uint8_t g_ledManualR = 0;
uint8_t g_ledManualG = 0;
uint8_t g_ledManualB = 0;
```

…and:

```cpp
String g_usbRx;
String g_piRx;

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
```

LEAVE in place (still in .ino for now): `uint16_t g_wheelBtnMask = 0;`, the USBHost block (`USBHost g_usbHost;` etc.), `bool g_wheelHostConnected = false;`, `uint32_t g_wheelHostButtons = 0;`, and `kWheelButtonColors[]`. These belong with their wheel logic and move in Task 5.

- [ ] **Step 4: Compile and verify memory parity**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. FLASH/RAM numbers should match baseline within a few bytes (the linker may reorder slightly).

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/state.h hardware-scripts/teensy-4.1/kart_controller/state.cpp hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino
git commit -m "refactor(teensy): extract state.{h,cpp} for shared globals

g_* globals (hall counter, arm state, output flags, throttle, LED bytes,
RX buffers, CAN instance) move from kart_controller.ino to state.cpp.
Wheel-specific globals stay in .ino until Task 5 moves them with the
wheel logic. Compile + memory parity verified.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Extract `outputs.{h,cpp}` for hardware output functions

Move the hardware-mutating helpers: ground-switch outputs, throttle DAC, speed-mode mux, and the raw LED PWM writer.

**Files:**
- Create: `outputs.h`, `outputs.cpp`
- Modify: `kart_controller.ino`

- [ ] **Step 1: Create `outputs.h`**

```cpp
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
```

- [ ] **Step 2: Create `outputs.cpp`**

```cpp
#include "outputs.h"

#include <Wire.h>

#include "pins.h"
#include "state.h"

void setGroundSwitchPin(uint8_t pin, bool asserted) {
  // Hardware path is MOSFET-switched ground. Command semantics use asserted=true
  // to mean "ground/activate ESC input".
  digitalWrite(pin, asserted ? HIGH : LOW);
}

void writeLedHardware(uint8_t r, uint8_t g, uint8_t b) {
  g_ledR = r;
  g_ledG = g;
  g_ledB = b;
  analogWrite(PIN_LED_RED, r);
  analogWrite(PIN_LED_GREEN, g);
  analogWrite(PIN_LED_BLUE, b);
}

void writeThrottleRaw(uint16_t raw) {
  raw = raw > THROTTLE_DAC_MAX ? THROTTLE_DAC_MAX : raw;

  Wire.beginTransmission(MCP4725_ADDR);
  Wire.write(0x40);                         // Fast mode write DAC register
  Wire.write((uint8_t)(raw >> 4));          // D11..D4
  Wire.write((uint8_t)((raw & 0x0F) << 4)); // D3..D0 xxxx
  Wire.endTransmission();
}

uint16_t setThrottlePercent(float percent) {
  if (percent < 0.0f) {
    percent = 0.0f;
  }
  if (percent > 100.0f) {
    percent = 100.0f;
  }

  g_throttlePct = percent;

  float v = THROTTLE_V_MIN + ((THROTTLE_V_MAX - THROTTLE_V_MIN) * (percent / 100.0f));
  float normalized = v / THROTTLE_DAC_REF;
  if (normalized < 0.0f) {
    normalized = 0.0f;
  }
  if (normalized > 1.0f) {
    normalized = 1.0f;
  }

  uint16_t raw = (uint16_t)roundf(normalized * THROTTLE_DAC_MAX);
  writeThrottleRaw(raw);
  return raw;
}

void setReverse(bool on) {
  g_reverseOn = on;
  setGroundSwitchPin(PIN_REVERSE, on);
}

void setBrake(bool on) {
  g_brakeOn = on;
  setGroundSwitchPin(PIN_BRAKE_LOW, on);
}

void setCruise(bool on) {
  g_cruiseOn = on;
  setGroundSwitchPin(PIN_CRUISE, on);
}

void setContactor(bool on) {
  g_contactorOn = on;
  setGroundSwitchPin(PIN_CONTACTOR, on);
}

void setSpeedLow(bool on) {
  g_speedLowOn = on;
  setGroundSwitchPin(PIN_SPEED_LOW, on);
}

void setSpeedHigh(bool on) {
  g_speedHighOn = on;
  setGroundSwitchPin(PIN_SPEED_HIGH, on);
}

void applySpeedMode(const char *mode) {
  if (strcmp(mode, "low") == 0) {
    setSpeedHigh(false);
    setSpeedLow(true);
  } else if (strcmp(mode, "high") == 0) {
    setSpeedLow(false);
    setSpeedHigh(true);
  } else {
    // medium/default => both deasserted
    setSpeedLow(false);
    setSpeedHigh(false);
  }
}
```

- [ ] **Step 3: Update `kart_controller.ino`**

Add `#include "outputs.h"` after `#include "state.h"`.

DELETE the following functions from the .ino (originally around lines 114–276):

- `setGroundSwitchPin`
- `writeLedHardware`
- `writeThrottleRaw`
- `setThrottlePercent`
- `setReverse`
- `setBrake`
- `setCruise`
- `setContactor`
- `setSpeedLow`
- `setSpeedHigh`
- `applySpeedMode`

The remaining functions in .ino (`refreshLedFromWheel`, `setLed`, `applySafeState`, etc.) all call into these extracted ones; the include resolves them.

- [ ] **Step 4: Compile and verify**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory parity within ±5% of baseline.

If the build fails with "undefined reference to setReverse" or similar, the .ino didn't get the `#include "outputs.h"` properly. Fix and re-run.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/outputs.h hardware-scripts/teensy-4.1/kart_controller/outputs.cpp hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino
git commit -m "refactor(teensy): extract outputs.{h,cpp} for hardware output helpers

Moves setReverse/Brake/Speed{Low,High}/Cruise/Contactor, throttle DAC,
LED PWM writes, ground-switch helper, and applySpeedMode out of the .ino
into a dedicated module. Compile + memory parity verified.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Extract `wheel.{h,cpp}` for wheel/LED logic + USB host

Move the wheel-button + LED color-mixing logic AND the USBHost objects into a dedicated module. The USBHost objects must live in this module's `.cpp` (file-scope linkage).

**Files:**
- Create: `wheel.h`, `wheel.cpp`
- Modify: `kart_controller.ino`

- [ ] **Step 1: Create `wheel.h`**

```cpp
#pragma once

#include <Arduino.h>

// Wheel button mask managed by Pi-side WHEEL_BTN commands.
extern uint16_t g_wheelBtnMask;

// USB host wheel state (mirror of g_joystick).
extern bool g_wheelHostConnected;
extern uint32_t g_wheelHostButtons;

// Highest WHEEL_BTN index accepted (0..WHEEL_BTN_COUNT-1).

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
```

- [ ] **Step 2: Create `wheel.cpp`**

```cpp
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
```

The `wheelJoystick*()` accessors exist so the WHEEL command (which lives in commands.cpp) doesn't need to see the `g_joystick` instance directly.

Note on include ordering: `g_wheelHostButtons`, `g_wheelHostConnected`, and `g_wheelBtnMask` are wheel-module-owned globals (defined in this file, declared `extern` in `wheel.h`). `g_ledManualR/G/B` are state-module-owned (declared in `state.h`, defined in `state.cpp`); `wheel.cpp` reads them, hence the `#include "state.h"`. The temporary `broadcastInfo` forward declaration goes away in Task 7.

- [ ] **Step 3: Update `kart_controller.ino`**

Add `#include "wheel.h"` after `#include "outputs.h"`.

DELETE from the .ino (originally around lines 62–95 plus 129–198):
- `uint16_t g_wheelBtnMask = 0;`
- The `WheelLedColor` struct + `kWheelButtonColors[]` array
- The USBHost block (`USBHost g_usbHost;` + 2 USBHub instances + `JoystickController g_joystick`)
- `bool g_wheelHostConnected = false;`
- `uint32_t g_wheelHostButtons = 0;`
- Functions: `combinedWheelButtons`, `refreshLedFromWheel`, `updateOnboardLedFromWheel`, `setLed`, `serviceUsbHostWheel`

The .ino's `setup()` function calls `g_usbHost.begin()` directly today. Replace that line with `wheelBegin();`.

The .ino's `loop()` calls `serviceUsbHostWheel()` — that resolves through wheel.h.

- [ ] **Step 4: Compile and verify**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory parity within ±5%.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/wheel.h hardware-scripts/teensy-4.1/kart_controller/wheel.cpp hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino
git commit -m "refactor(teensy): extract wheel.{h,cpp} for wheel + USB host LED logic

Moves the per-button color table, USBHost/Hub/JoystickController
instances, refreshLedFromWheel, updateOnboardLedFromWheel, setLed, and
serviceUsbHostWheel out of the .ino. Wheel-state globals (g_wheelBtnMask,
g_wheelHostConnected, g_wheelHostButtons) move with them. Joystick
identity exposed via wheelJoystick{Vid,Pid,Type}() accessors so commands
can read it without a direct g_joystick reference.

Temporary forward declaration of broadcastInfo until Task 7 lands parser.h.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Extract `safety.{h,cpp}` for arm-window state machine

`isArmed`, `armRemainingMs`, `applySafeState`, `serviceArmTimeout` belong together — they govern the ARM/DISARM/SAFE state machine.

**Files:**
- Create: `safety.h`, `safety.cpp`
- Modify: `kart_controller.ino`

- [ ] **Step 1: Create `safety.h`**

```cpp
#pragma once

#include <Arduino.h>

bool isArmed();
uint32_t armRemainingMs();

// Drive every safety-gated output to a deterministic safe state.
void applySafeState();

// Watchdog: if the ARM window expired this loop, apply safe state and broadcast.
void serviceArmTimeout();
```

- [ ] **Step 2: Create `safety.cpp`**

```cpp
#include "safety.h"

#include "outputs.h"
#include "wheel.h"
#include "state.h"

// Forward declaration; provided by parser.h after Task 7.
void broadcastInfo(const char *msg);

bool isArmed() {
  return (int32_t)(g_armUntilMs - millis()) > 0;
}

uint32_t armRemainingMs() {
  if (!isArmed()) {
    return 0;
  }
  return g_armUntilMs - millis();
}

void applySafeState() {
  setReverse(false);
  setBrake(false);
  setCruise(false);
  setContactor(false);
  applySpeedMode("medium");
  setThrottlePercent(0.0f);
  setLed(0, 0, 0);
}

void serviceArmTimeout() {
  bool armedNow = isArmed();
  if (g_wasArmedLastLoop && !armedNow) {
    applySafeState();
    broadcastInfo("INFO ARM_EXPIRED SAFE_APPLIED");
  }
  g_wasArmedLastLoop = armedNow;
}
```

- [ ] **Step 3: Update `kart_controller.ino`**

Add `#include "safety.h"` after `#include "wheel.h"`.

DELETE from the .ino: `isArmed`, `armRemainingMs`, `applySafeState`, `serviceArmTimeout` function definitions (originally around lines 103–112 + 278–286 + 860–867).

`setup()` calls `applySafeState()` — resolves via safety.h. `loop()` calls `serviceArmTimeout()` — same.

- [ ] **Step 4: Compile and verify**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory parity within ±5%.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/safety.h hardware-scripts/teensy-4.1/kart_controller/safety.cpp hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino
git commit -m "refactor(teensy): extract safety.{h,cpp} for arm-window state machine

isArmed, armRemainingMs, applySafeState, and serviceArmTimeout move out
of the .ino into a dedicated module. Same temporary broadcastInfo
forward declaration as wheel.cpp until Task 7.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Extract `parser.{h,cpp}` for line tokenizer + utility parsers

Move the parsing primitives (`parseOnOff`, `parseFloatStrict`, `parseUInt32Strict`, `hexNibble`, `parseHexBytes`, `printHexByte`), the arm guard (`requireArmed`), the broadcast helper (`broadcastInfo`), the per-port reader (`servicePort`), and the hall-pulse ISR (`onHallPulse`) into one module.

After this task, the temporary forward declarations of `broadcastInfo` in `wheel.cpp` and `safety.cpp` get replaced with `#include "parser.h"`.

**Files:**
- Create: `parser.h`, `parser.cpp`
- Modify: `kart_controller.ino`, `wheel.cpp`, `safety.cpp`

- [ ] **Step 1: Create `parser.h`**

```cpp
#pragma once

#include <Arduino.h>

bool parseOnOff(const char *token, bool &out);
bool parseFloatStrict(const char *token, float &out);
bool parseUInt32Strict(const char *token, uint32_t &out);

int hexNibble(char c);
int parseHexBytes(const char *text, uint8_t *out, int maxLen);

void printHexByte(Stream &out, uint8_t v);

bool requireArmed(Stream &out);
void broadcastInfo(const char *msg);

// Read characters from `port`, accumulating into `buffer`. On newline/return,
// invoke handleCommand(buffer, port) and clear the buffer.
void servicePort(Stream &port, String &buffer);

// Hall-pulse ISR. Attach via attachInterrupt(...).
void onHallPulse();
```

- [ ] **Step 2: Create `parser.cpp`**

```cpp
#include "parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "safety.h"
#include "state.h"

// Forward declaration; provided by commands.h after Task 8.
void handleCommand(const String &lineIn, Stream &out);

bool parseOnOff(const char *token, bool &out) {
  if (!token) {
    return false;
  }
  if (strcmp(token, "1") == 0 || strcasecmp(token, "on") == 0 || strcasecmp(token, "true") == 0) {
    out = true;
    return true;
  }
  if (strcmp(token, "0") == 0 || strcasecmp(token, "off") == 0 || strcasecmp(token, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

bool parseFloatStrict(const char *token, float &out) {
  if (!token) {
    return false;
  }
  char *end = nullptr;
  out = strtof(token, &end);
  return end && *end == '\0';
}

bool parseUInt32Strict(const char *token, uint32_t &out) {
  if (!token) {
    return false;
  }
  char *end = nullptr;
  unsigned long val = strtoul(token, &end, 0);
  if (!(end && *end == '\0')) {
    return false;
  }
  out = (uint32_t)val;
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

int parseHexBytes(const char *text, uint8_t *out, int maxLen) {
  if (!text) {
    return -1;
  }

  char clean[192];
  int cleanLen = 0;

  for (size_t i = 0; text[i] != '\0' && cleanLen < (int)sizeof(clean) - 1; i++) {
    char c = text[i];
    if (isxdigit((unsigned char)c)) {
      clean[cleanLen++] = c;
    }
  }
  clean[cleanLen] = '\0';

  if (cleanLen == 0 || (cleanLen % 2) != 0) {
    return -1;
  }

  int bytes = cleanLen / 2;
  if (bytes > maxLen) {
    return -2;
  }

  for (int i = 0; i < bytes; i++) {
    int hi = hexNibble(clean[i * 2]);
    int lo = hexNibble(clean[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }

  return bytes;
}

void printHexByte(Stream &out, uint8_t v) {
  static const char *kHexDigits = "0123456789ABCDEF";
  out.print(kHexDigits[(v >> 4) & 0x0F]);
  out.print(kHexDigits[v & 0x0F]);
}

bool requireArmed(Stream &out) {
  if (isArmed()) {
    return true;
  }
  out.println("ERR NOT_ARMED (send: ARM <seconds>)");
  return false;
}

void broadcastInfo(const char *msg) {
  if (Serial) {
    Serial.println(msg);
  }
  Serial2.println(msg);
}

void servicePort(Stream &port, String &buffer) {
  while (port.available() > 0) {
    char c = (char)port.read();
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        String line = buffer;
        buffer = "";
        handleCommand(line, port);
      }
    } else {
      if (buffer.length() < 180) {
        buffer += c;
      }
    }
  }
}

void onHallPulse() {
  g_hallPulseCount++;
}
```

- [ ] **Step 3: Update `wheel.cpp` and `safety.cpp` to drop their forward declarations of `broadcastInfo`**

In `wheel.cpp`: remove the line:

```cpp
// Temporary forward declaration; provided by parser.h after Task 7.
void broadcastInfo(const char *msg);
```

…and add `#include "parser.h"` to the include block.

In `safety.cpp`: remove the line:

```cpp
// Forward declaration; provided by parser.h after Task 7.
void broadcastInfo(const char *msg);
```

…and add `#include "parser.h"` to the include block.

- [ ] **Step 4: Update `kart_controller.ino`**

Add `#include "parser.h"` after `#include "safety.h"`.

DELETE from the .ino: `parseOnOff`, `parseFloatStrict`, `parseUInt32Strict`, `hexNibble`, `parseHexBytes`, `printHexByte`, `requireArmed`, `broadcastInfo`, `servicePort`, `onHallPulse` (originally around lines 99–101 + 288–394 + 843–858 + 920–924).

The `loop()` calls `servicePort(Serial, g_usbRx);` — resolves via parser.h.
The `setup()` calls `attachInterrupt(digitalPinToInterrupt(PIN_HALL_PULSES), onHallPulse, RISING);` — resolves via parser.h.

- [ ] **Step 5: Compile and verify**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory parity within ±5%.

- [ ] **Step 6: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/parser.h hardware-scripts/teensy-4.1/kart_controller/parser.cpp hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino hardware-scripts/teensy-4.1/kart_controller/wheel.cpp hardware-scripts/teensy-4.1/kart_controller/safety.cpp
git commit -m "refactor(teensy): extract parser.{h,cpp} for tokenizer + parse helpers

Moves parseOnOff/Float/UInt32Strict, hexNibble, parseHexBytes,
printHexByte, requireArmed, broadcastInfo, servicePort, and onHallPulse
out of the .ino. Resolves the temporary broadcastInfo forward
declarations in wheel.cpp and safety.cpp.

handleCommand still lives in the .ino — it moves to commands.cpp in the
next task along with the dispatch refactor.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Move `handleCommand` + helpers to `commands.{h,cpp}` (verbatim)

Move `applyNamedOutput`, `printHelp`, `printStatus`, and `handleCommand` from the .ino into a new `commands.{h,cpp}` module. The `handleCommand` body stays as-is — the if/else cascade is preserved verbatim. The dispatcher refactor happens in Task 9.

**Files:**
- Create: `commands.h`, `commands.cpp`
- Modify: `kart_controller.ino`, `parser.cpp`

- [ ] **Step 1: Create `commands.h`**

```cpp
#pragma once

#include <Arduino.h>

void handleCommand(const String &lineIn, Stream &out);
void printHelp(Stream &out);
void printStatus(Stream &out);
bool applyNamedOutput(const char *name, bool asserted, Stream &out);
```

- [ ] **Step 2: Create `commands.cpp`**

Copy these definitions verbatim from `kart_controller.ino` into a new `commands.cpp`. Add the includes:

```cpp
#include "commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <FlexCAN_T4.h>

#include "outputs.h"
#include "parser.h"
#include "safety.h"
#include "state.h"
#include "wheel.h"
```

Then the existing function bodies, in this order:

1. `printHelp(Stream &out)` — verbatim from the .ino's current line ~396.
2. `printStatus(Stream &out)` — verbatim from the .ino's line ~400.
3. `applyNamedOutput(const char *name, bool asserted, Stream &out)` — verbatim from line ~434.
4. `handleCommand(const String &lineIn, Stream &out)` — verbatim from line ~467, BUT with one modification: the `WHEEL` and `WHEEL?` branch currently directly accesses `g_joystick.idVendor()`, `g_joystick.idProduct()`, `g_joystick.joystickType()`. Replace those calls with `wheelJoystickVid()`, `wheelJoystickPid()`, `wheelJoystickType()` (declared in `wheel.h` from Task 5).

The handleCommand WHEEL branch:

```cpp
if (cmd == "WHEEL" || cmd == "WHEEL?") {
  out.print("OK WHEEL host_connected=");
  out.print(g_wheelHostConnected ? 1 : 0);
  if (g_wheelHostConnected) {
    out.print(" vid=0x");
    out.print(wheelJoystickVid(), HEX);
    out.print(" pid=0x");
    out.print(wheelJoystickPid(), HEX);
    out.print(" type=");
    out.print(wheelJoystickType());
  }
  out.print(" host_buttons=0x");
  out.print(g_wheelHostButtons, HEX);
  out.print(" pi_buttons=0x");
  out.println(g_wheelBtnMask, HEX);
  return;
}
```

(The original referenced `g_joystick.idVendor()` etc.; the wrapper functions in wheel.cpp now expose those.)

Everything else inside `handleCommand` stays byte-identical to the original.

- [ ] **Step 3: Update `parser.cpp` to drop its forward declaration of `handleCommand`**

Remove the line:

```cpp
// Forward declaration; provided by commands.h after Task 8.
void handleCommand(const String &lineIn, Stream &out);
```

…and add `#include "commands.h"` to the include block.

- [ ] **Step 4: Update `kart_controller.ino`**

Add `#include "commands.h"` after `#include "parser.h"`.

DELETE from the .ino: `printHelp`, `printStatus`, `applyNamedOutput`, `handleCommand` function definitions (originally around lines 396–841).

After this delete, the .ino should contain only:
- The remaining top-level includes (`<Arduino.h>`, `<Wire.h>`, `<FlexCAN_T4.h>`, `<USBHost_t36.h>`, `<ctype.h>`, `<stdlib.h>`, `<string.h>` — note: most are no longer strictly needed but harmless).
- The local includes (`pins.h`, `state.h`, `outputs.h`, `wheel.h`, `safety.h`, `parser.h`, `commands.h`).
- `setup()` function.
- `loop()` function.

Approximate line count: ~80 lines, down from 925.

- [ ] **Step 5: Compile and verify response-text identity for the WHEEL command path**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory parity within ±5%.

The WHEEL command's response text was the only thing changed in this task — the wrapper functions (`wheelJoystickVid()` etc.) are inline `return` statements that produce identical values to the direct `g_joystick.idVendor()` calls. The `out.print(..., HEX)` formatting is unchanged. **Sanity check:** read both versions of the WHEEL branch and confirm they produce identical output for the same `g_joystick` state.

- [ ] **Step 6: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/commands.h hardware-scripts/teensy-4.1/kart_controller/commands.cpp hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino hardware-scripts/teensy-4.1/kart_controller/parser.cpp
git commit -m "refactor(teensy): move handleCommand + helpers to commands.{h,cpp}

Moves applyNamedOutput, printHelp, printStatus, handleCommand verbatim
from the .ino. Only behavioral change: WHEEL/WHEEL? branch now reads
joystick identity via wheelJoystick{Vid,Pid,Type}() accessors instead
of touching g_joystick directly. Output text is byte-identical.

The if/else dispatch cascade inside handleCommand is preserved as-is;
the next task replaces it with a command-table.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Refactor `handleCommand` to use a command-table dispatch

Replace the 374-line `if/else` cascade with a `CommandHandler` table and per-command handler functions. **This is the most behaviorally-sensitive task.** Per-command response text must be reviewed against the pre-refactor output.

**Files:**
- Modify: `commands.h`, `commands.cpp`

- [ ] **Step 1: Add the `CommandHandler` type to `commands.h`**

```cpp
#pragma once

#include <Arduino.h>

void handleCommand(const String &lineIn, Stream &out);
void printHelp(Stream &out);
void printStatus(Stream &out);
bool applyNamedOutput(const char *name, bool asserted, Stream &out);

struct CommandHandler {
  const char *name;                                  // uppercase command name
  void (*handle)(int argc, char **argv, Stream &out);
};

extern const CommandHandler kCommands[];
extern const size_t kCommandCount;
```

- [ ] **Step 2: Replace the body of `commands.cpp`**

Overwrite `commands.cpp` with the table-driven dispatcher. The full content is below — **read carefully**, especially the per-command handlers which must produce identical response text to the pre-refactor `if/else` arms.

```cpp
#include "commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <FlexCAN_T4.h>

#include "outputs.h"
#include "parser.h"
#include "safety.h"
#include "state.h"
#include "wheel.h"

void printHelp(Stream &out) {
  out.println("OK HELP PING|STATUS|ARM|DISARM|SAFE|OUTPUT|SPEED|BRAKE|REVERSE|CONTACTOR|THROTTLE|LED|HALL?|ESC_WRITE|ESC_READ|CAN_TX|CAN_POLL|WHEEL_BTN|WHEEL?");
}

void printStatus(Stream &out) {
  uint32_t hallCopy;
  noInterrupts();
  hallCopy = g_hallPulseCount;
  interrupts();

  out.print("OK STATUS armed=");
  out.print(isArmed() ? 1 : 0);
  out.print(" arm_ms=");
  out.print(armRemainingMs());
  out.print(" throttle_pct=");
  out.print(g_throttlePct, 2);
  out.print(" reverse=");
  out.print(g_reverseOn ? 1 : 0);
  out.print(" brake=");
  out.print(g_brakeOn ? 1 : 0);
  out.print(" speed_low=");
  out.print(g_speedLowOn ? 1 : 0);
  out.print(" speed_high=");
  out.print(g_speedHighOn ? 1 : 0);
  out.print(" cruise=");
  out.print(g_cruiseOn ? 1 : 0);
  out.print(" contactor=");
  out.print(g_contactorOn ? 1 : 0);
  out.print(" led=");
  out.print(g_ledR);
  out.print(",");
  out.print(g_ledG);
  out.print(",");
  out.print(g_ledB);
  out.print(" hall=");
  out.println(hallCopy);
}

bool applyNamedOutput(const char *name, bool asserted, Stream &out) {
  if (strcasecmp(name, "reverse") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setReverse(asserted);
  } else if (strcasecmp(name, "brake") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setBrake(asserted);
  } else if (strcasecmp(name, "speed_low") == 0) {
    if (asserted && !requireArmed(out)) return false;
    if (asserted) setSpeedHigh(false);
    setSpeedLow(asserted);
  } else if (strcasecmp(name, "speed_high") == 0) {
    if (asserted && !requireArmed(out)) return false;
    if (asserted) setSpeedLow(false);
    setSpeedHigh(asserted);
  } else if (strcasecmp(name, "cruise") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setCruise(asserted);
  } else if (strcasecmp(name, "contactor") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setContactor(asserted);
  } else {
    out.println("ERR OUTPUT_NAME");
    return false;
  }

  out.print("OK OUTPUT ");
  out.print(name);
  out.print("=");
  out.println(asserted ? "on" : "off");
  return true;
}

// --- Per-command handlers --------------------------------------------------

static void cmdPing(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  out.println("OK PONG");
}

static void cmdHelp(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  printHelp(out);
}

static void cmdStatus(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  printStatus(out);
}

static void cmdArm(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR ARM seconds_required");
    return;
  }
  float seconds;
  if (!parseFloatStrict(argv[1], seconds) || seconds <= 0.0f || seconds > 30.0f) {
    out.println("ERR ARM seconds_range_0_30");
    return;
  }

  g_armUntilMs = millis() + (uint32_t)(seconds * 1000.0f);
  out.print("OK ARM seconds=");
  out.println(seconds, 2);
}

static void cmdDisarm(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  g_armUntilMs = 0;
  applySafeState();
  out.println("OK DISARMED SAFE");
}

static void cmdSafe(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  g_armUntilMs = 0;
  applySafeState();
  out.println("OK SAFE");
}

static void cmdOutput(int argc, char **argv, Stream &out) {
  if (argc < 3) {
    out.println("ERR OUTPUT usage: OUTPUT <name> <on|off>");
    return;
  }
  bool on;
  if (!parseOnOff(argv[2], on)) {
    out.println("ERR OUTPUT state_on_off");
    return;
  }
  (void)applyNamedOutput(argv[1], on, out);
}

static void cmdSpeed(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR SPEED mode_required");
    return;
  }

  String mode = String(argv[1]);
  mode.toLowerCase();
  if (mode == "low" || mode == "high") {
    if (!requireArmed(out)) return;
  }

  if (mode == "low" || mode == "medium" || mode == "high") {
    applySpeedMode(mode.c_str());
    out.print("OK SPEED mode=");
    out.println(mode);
    return;
  }

  out.println("ERR SPEED mode_low_medium_high");
}

// Shared by BRAKE / REVERSE / CONTACTOR — each is a thin alias around
// applyNamedOutput that returns its uppercase name for the OK line.
static void cmdNamedOutputAlias(int argc, char **argv, Stream &out, const char *name, const char *upperLabel) {
  if (argc < 2) {
    out.println("ERR STATE on_off_required");
    return;
  }
  bool on;
  if (!parseOnOff(argv[1], on)) {
    out.println("ERR STATE on_off_required");
    return;
  }

  if (on && !requireArmed(out)) return;

  if (strcmp(name, "brake") == 0) setBrake(on);
  else if (strcmp(name, "reverse") == 0) setReverse(on);
  else if (strcmp(name, "contactor") == 0) setContactor(on);

  out.print("OK ");
  out.print(upperLabel);
  out.print("=");
  out.println(on ? "on" : "off");
}

static void cmdBrake(int argc, char **argv, Stream &out) {
  cmdNamedOutputAlias(argc, argv, out, "brake", "BRAKE");
}

static void cmdReverse(int argc, char **argv, Stream &out) {
  cmdNamedOutputAlias(argc, argv, out, "reverse", "REVERSE");
}

static void cmdContactor(int argc, char **argv, Stream &out) {
  cmdNamedOutputAlias(argc, argv, out, "contactor", "CONTACTOR");
}

static void cmdThrottle(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR THROTTLE percent_required");
    return;
  }

  float pct;
  if (!parseFloatStrict(argv[1], pct)) {
    out.println("ERR THROTTLE percent_number");
    return;
  }

  if (pct > 0.0f && !requireArmed(out)) {
    return;
  }

  uint16_t raw = setThrottlePercent(pct);
  out.print("OK THROTTLE pct=");
  out.print(g_throttlePct, 2);
  out.print(" dac=");
  out.println(raw);
}

static void cmdLed(int argc, char **argv, Stream &out) {
  if (argc < 4) {
    out.println("ERR LED usage: LED <r> <g> <b>");
    return;
  }

  uint32_t r, g, b;
  if (!parseUInt32Strict(argv[1], r) || !parseUInt32Strict(argv[2], g) || !parseUInt32Strict(argv[3], b)) {
    out.println("ERR LED values_0_255");
    return;
  }
  if (r > 255 || g > 255 || b > 255) {
    out.println("ERR LED values_0_255");
    return;
  }

  setLed((uint8_t)r, (uint8_t)g, (uint8_t)b);
  out.print("OK LED ");
  out.print((int)g_ledR);
  out.print(" ");
  out.print((int)g_ledG);
  out.print(" ");
  out.println((int)g_ledB);
}

static void cmdHall(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  uint32_t hallCopy;
  noInterrupts();
  hallCopy = g_hallPulseCount;
  interrupts();

  out.print("OK HALL count=");
  out.println(hallCopy);
}

static void cmdEscWrite(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR ESC_WRITE hexbytes_required");
    return;
  }
  if (!requireArmed(out)) return;

  uint8_t payload[64];
  int len = parseHexBytes(argv[1], payload, (int)sizeof(payload));
  if (len <= 0) {
    out.println("ERR ESC_WRITE hexbytes_invalid");
    return;
  }

  size_t written = Serial1.write(payload, (size_t)len);
  Serial1.flush();

  out.print("OK ESC_WRITE bytes=");
  out.println((int)written);
}

static void cmdEscRead(int argc, char **argv, Stream &out) {
  uint32_t maxBytes = 64;
  if (argc >= 2) {
    if (!parseUInt32Strict(argv[1], maxBytes) || maxBytes == 0) {
      out.println("ERR ESC_READ max_bytes_invalid");
      return;
    }
    if (maxBytes > 256) {
      maxBytes = 256;
    }
  }

  out.print("OK ESC_READ ");
  uint32_t count = 0;
  while (Serial1.available() && count < maxBytes) {
    uint8_t b = (uint8_t)Serial1.read();
    printHexByte(out, b);
    count++;
  }
  out.println();
}

static void cmdCanTx(int argc, char **argv, Stream &out) {
  if (argc < 3) {
    out.println("ERR CAN_TX usage: CAN_TX <id> <hexbytes>");
    return;
  }
  if (!requireArmed(out)) return;

  uint32_t id;
  if (!parseUInt32Strict(argv[1], id)) {
    out.println("ERR CAN_TX id_invalid");
    return;
  }

  uint8_t data[8];
  int len = parseHexBytes(argv[2], data, 8);
  if (len < 0) {
    out.println("ERR CAN_TX data_invalid_hex");
    return;
  }

  CAN_message_t msg;
  msg.id = id;
  msg.flags.extended = 0;
  msg.len = len;
  for (int i = 0; i < len; i++) {
    msg.buf[i] = data[i];
  }

  bool ok = Can0.write(msg);
  if (!ok) {
    out.println("ERR CAN_TX write_failed");
    return;
  }

  out.println("OK CAN_TX");
}

static void cmdCanPoll(int argc, char **argv, Stream &out) {
  uint32_t maxFrames = 8;
  if (argc >= 2) {
    if (!parseUInt32Strict(argv[1], maxFrames) || maxFrames == 0) {
      out.println("ERR CAN_POLL max_frames_invalid");
      return;
    }
    if (maxFrames > 64) {
      maxFrames = 64;
    }
  }

  uint32_t readCount = 0;
  CAN_message_t msg;
  while (readCount < maxFrames && Can0.read(msg)) {
    out.print("CAN id=0x");
    out.print(msg.id, HEX);
    out.print(" len=");
    out.print(msg.len);
    out.print(" data=");
    for (uint8_t i = 0; i < msg.len; i++) {
      printHexByte(out, msg.buf[i]);
    }
    out.println();
    readCount++;
  }

  out.print("OK CAN_POLL count=");
  out.println(readCount);
}

static void cmdWheel(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  out.print("OK WHEEL host_connected=");
  out.print(g_wheelHostConnected ? 1 : 0);
  if (g_wheelHostConnected) {
    out.print(" vid=0x");
    out.print(wheelJoystickVid(), HEX);
    out.print(" pid=0x");
    out.print(wheelJoystickPid(), HEX);
    out.print(" type=");
    out.print(wheelJoystickType());
  }
  out.print(" host_buttons=0x");
  out.print(g_wheelHostButtons, HEX);
  out.print(" pi_buttons=0x");
  out.println(g_wheelBtnMask, HEX);
}

static void cmdWheelBtn(int argc, char **argv, Stream &out) {
  if (argc < 3) {
    out.println("ERR WHEEL_BTN usage: WHEEL_BTN <idx> <0|1>");
    return;
  }

  uint32_t idx;
  bool pressed;
  if (!parseUInt32Strict(argv[1], idx) || idx >= WHEEL_BTN_COUNT) {
    out.print("ERR WHEEL_BTN idx_range_0_");
    out.println(WHEEL_BTN_COUNT - 1);
    return;
  }
  if (!parseOnOff(argv[2], pressed)) {
    out.println("ERR WHEEL_BTN state_0_1");
    return;
  }

  uint16_t bit = (uint16_t)1 << idx;
  if (pressed) {
    g_wheelBtnMask |= bit;
  } else {
    g_wheelBtnMask &= (uint16_t)~bit;
  }
  updateOnboardLedFromWheel();

  out.print("OK WHEEL_BTN idx=");
  out.print((int)idx);
  out.print(" state=");
  out.print(pressed ? 1 : 0);
  out.print(" mask=0x");
  out.println(g_wheelBtnMask, HEX);
}

// --- Command table ---------------------------------------------------------

const CommandHandler kCommands[] = {
  {"PING",       cmdPing},
  {"HELP",       cmdHelp},
  {"STATUS",     cmdStatus},
  {"ARM",        cmdArm},
  {"DISARM",     cmdDisarm},
  {"SAFE",       cmdSafe},
  {"OUTPUT",     cmdOutput},
  {"SPEED",      cmdSpeed},
  {"BRAKE",      cmdBrake},
  {"REVERSE",    cmdReverse},
  {"CONTACTOR",  cmdContactor},
  {"THROTTLE",   cmdThrottle},
  {"LED",        cmdLed},
  {"HALL",       cmdHall},
  {"HALL?",      cmdHall},
  {"ESC_WRITE",  cmdEscWrite},
  {"ESC_READ",   cmdEscRead},
  {"CAN_TX",     cmdCanTx},
  {"CAN_POLL",   cmdCanPoll},
  {"WHEEL",      cmdWheel},
  {"WHEEL?",     cmdWheel},
  {"WHEEL_BTN",  cmdWheelBtn},
};

const size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

void handleCommand(const String &lineIn, Stream &out) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) {
    return;
  }

  char buf[192];
  line.toCharArray(buf, sizeof(buf));

  char *tokens[10];
  int n = 0;
  char *tok = strtok(buf, " \t");
  while (tok && n < 10) {
    tokens[n++] = tok;
    tok = strtok(nullptr, " \t");
  }

  if (n == 0) {
    return;
  }

  String cmd = String(tokens[0]);
  cmd.toUpperCase();

  for (size_t i = 0; i < kCommandCount; ++i) {
    if (cmd == kCommands[i].name) {
      kCommands[i].handle(n, tokens, out);
      return;
    }
  }

  out.println("ERR UNKNOWN_CMD");
}
```

- [ ] **Step 3: Compile**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Expected: exit 0. Memory parity within ±5%. The command-table itself is ~22 entries × ~12 bytes = ~264 bytes of FLASH; the per-command static functions inline most of the logic; net change should be small.

- [ ] **Step 4: Per-command response-text review**

For each of the 17 commands listed below, **read the corresponding handler in the new `commands.cpp` and the corresponding branch in the pre-refactor `kart_controller.ino`** (use `git show 8466e4e:hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino` to view the pre-refactor source). Confirm the response text format is byte-identical:

- `PING` → `OK PONG`
- `HELP` → see `printHelp` (table-of-commands line)
- `STATUS` → see `printStatus` (key=value list)
- `ARM <s>` → `OK ARM seconds=<s>` or `ERR ARM seconds_required` / `seconds_range_0_30`
- `DISARM` → `OK DISARMED SAFE`
- `SAFE` → `OK SAFE`
- `OUTPUT <name> <on|off>` → see `applyNamedOutput` `OK OUTPUT name=on/off` or `ERR OUTPUT_NAME` / `ERR OUTPUT usage: ...` / `ERR OUTPUT state_on_off`
- `SPEED <mode>` → `OK SPEED mode=<mode>` or `ERR SPEED mode_required` / `mode_low_medium_high`
- `BRAKE <on|off>` / `REVERSE <on|off>` / `CONTACTOR <on|off>` → `OK BRAKE=on/off` (and equivalents) or `ERR STATE on_off_required`
- `THROTTLE <pct>` → `OK THROTTLE pct=<pct> dac=<raw>` or `ERR THROTTLE percent_required` / `percent_number`
- `LED <r> <g> <b>` → `OK LED <r> <g> <b>` or `ERR LED usage: ...` / `ERR LED values_0_255`
- `HALL` / `HALL?` → `OK HALL count=<n>`
- `ESC_WRITE <hex>` → `OK ESC_WRITE bytes=<n>` or `ERR ESC_WRITE hexbytes_required` / `hexbytes_invalid`
- `ESC_READ [max]` → `OK ESC_READ <hex...>\n` or `ERR ESC_READ max_bytes_invalid`
- `CAN_TX <id> <hex>` → `OK CAN_TX` or `ERR CAN_TX usage: ...` / `id_invalid` / `data_invalid_hex` / `write_failed`
- `CAN_POLL [max]` → `CAN id=0x<n> len=<n> data=<hex>\nOK CAN_POLL count=<n>` or `ERR CAN_POLL max_frames_invalid`
- `WHEEL` / `WHEEL?` → `OK WHEEL host_connected=<n> ...`
- `WHEEL_BTN <idx> <0|1>` → `OK WHEEL_BTN idx=<n> state=<n> mask=0x<n>` or `ERR WHEEL_BTN idx_range_0_<N-1>` / `state_0_1`

If ANY response text differs (different word order, different key names, missing newline, extra whitespace), STOP and fix the handler before committing.

Watch especially for:
- `cmdNamedOutputAlias`'s output format (`OK BRAKE=on` etc.) — the upperLabel parameter must produce exactly `BRAKE`, `REVERSE`, `CONTACTOR` (uppercase).
- `cmdSpeed` uses `String mode` and lowercases it — `OK SPEED mode=low`/`medium`/`high`.
- The dispatcher's unknown-command path produces `ERR UNKNOWN_CMD`.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/teensy-4.1/kart_controller/commands.h hardware-scripts/teensy-4.1/kart_controller/commands.cpp
git commit -m "refactor(teensy): replace handleCommand if/else with command table

Replaces the 374-line if/else cascade inside handleCommand with a
CommandHandler table + per-command handler functions. BRAKE/REVERSE/
CONTACTOR now share cmdNamedOutputAlias, eliminating the inline
duplication of the OUTPUT command's parse-and-arm-check logic.

Per-command response text reviewed against pre-refactor source for all
17 commands. Output is byte-identical.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Final verification + cleanup

Compile gate, memory parity, .ino sanity, baseline file deletion.

**Files:**
- Read-only verification.
- Delete: `docs/superpowers/specs/teensy-refactor-baseline.txt`

- [ ] **Step 1: Final compile + memory parity**

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller 2>&1 | tail -5
```

Compare the FLASH/RAM numbers against `docs/superpowers/specs/teensy-refactor-baseline.txt`. Both `code:` and `variables:` for FLASH and RAM1 must be within ±5% of the baseline. Document the actual values for the PR description.

- [ ] **Step 2: Verify .ino is now slim**

```bash
wc -l hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino
```

Expected: ≤100 lines (down from 925). Capture the actual count.

- [ ] **Step 3: List all module files**

```bash
ls -la hardware-scripts/teensy-4.1/kart_controller/
```

Expected files (15 total):
- `kart_controller.ino`
- `pins.h`
- `state.h`, `state.cpp`
- `outputs.h`, `outputs.cpp`
- `wheel.h`, `wheel.cpp`
- `safety.h`, `safety.cpp`
- `parser.h`, `parser.cpp`
- `commands.h`, `commands.cpp`

(Plus any `.gitignore` or hidden files that already existed.)

- [ ] **Step 4: Delete the baseline capture**

The baseline served its purpose; it's a snapshot, not a long-lived reference.

```bash
git rm docs/superpowers/specs/teensy-refactor-baseline.txt
```

- [ ] **Step 5: Commit the cleanup**

```bash
git commit -m "chore(teensy): drop refactor baseline capture

Compile gate verified across all 9 task commits; baseline served its
purpose. Hardware smoke (kartctl ping/status/validate bringup against a
flashed Teensy) deferred to deploy-test on the Pi.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 6: Print a summary**

```bash
git log --oneline main..HEAD
git diff main..HEAD --stat | tail -20
```

The list should trace the migration: spec → plan → baseline → 7 module extractions → command-table refactor → cleanup. Use this list when writing the PR description.

---

## Done criteria

- All 10 tasks above have their checkboxes ticked.
- `arduino-cli compile --fqbn teensy:avr:teensy41 hardware-scripts/teensy-4.1/kart_controller` exits 0 at every commit.
- Final memory footprint is within ±5% of baseline (FLASH code + data, RAM1 variables, RAM2 variables).
- `kart_controller.ino` is ≤100 lines.
- 13 new files in `hardware-scripts/teensy-4.1/kart_controller/`.
- `git diff main..HEAD --stat` shows changes only in `hardware-scripts/teensy-4.1/kart_controller/` and `docs/superpowers/`. No changes to React, Python, deploy, or any unrelated path.
- Per-command response-text review (Task 9 Step 4) was done — confirm the commit message documents which commands were checked.
- Hardware smoke (flash a Teensy, run `kartctl ping`/`status`/`validate bringup`) is documented in the PR description as deferred to deploy-test if hardware was unavailable at PR time.

When all of the above hold, the Teensy sub-project is complete. With sub-project 3 of 3 done, the multi-layer refactor is finished.
