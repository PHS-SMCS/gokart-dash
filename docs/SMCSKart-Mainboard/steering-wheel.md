# USB Steering Wheel (Hori Racing Wheel Overdrive)

The SMCSKart bench uses a **Hori Racing Wheel Overdrive for Xbox Series X** as an operator input device. It is a USB HID device (not XInput-only), so it enumerates under the Linux `xpad`/HID gamepad driver without extra setup.

## Identification

| Field | Value |
|-------|-------|
| Vendor ID | `0f0d` (Hori Co., Ltd.) |
| Product ID | `0152` |
| Device name | `Hori Racing Wheel Overdrive for Xbox Series X` |
| USB interfaces | `ff47d0` (Xbox HID class) |
| Force feedback | Reported (`FF=107030000`) — not currently used |

## Wiring

The wheel plugs into the **mainboard USB-A port** (primary path), which routes to the Teensy 4.1 USB Host header. The Teensy firmware enumerates HID/XInput wheels via `USBHost_t36` and drives logic from the wheel state directly.

A **Raspberry Pi USB port** is supported as a fallback path — useful when the Teensy USB Host cannot bind a specific device. In that mode `wheel_bridge.py` on the Pi forwards events to the Teensy over the Pi↔Teensy UART.

## Linux device nodes

| Node | Purpose |
|------|---------|
| `/dev/input/js0` | Legacy joystick API — easy to consume with 8-byte `struct js_event` reads |
| `/dev/input/event5` | evdev API — richer event types, force-feedback output |
| `/dev/input/by-id/usb-HORI_*-joystick` | Stable symlink, prefer this over `js0` |

Access requires membership in the `input` group (the `gokart` user is already a member).

## Input map (observed via `wheel_probe.py`)

### Axes (signed 16-bit, `-32768..+32767`)

| # | Name | Resting | Description | Notes |
|---|------|---------|-------------|-------|
| 0 | LX | 0 | **Steering wheel** | Centered at 0, full left → negative, full right → positive. Physical lock-to-lock does not reach the extremes; treat the usable range as a tunable calibration. |
| 1 | LY | 0 | (unused) | No physical control mapped; stays at 0. |
| 2 | LT | -32767 | **Brake pedal** | -32767 = released, +32767 = fully pressed. |
| 3 | RX | 0 | (unused) | |
| 4 | RY | 0 | (unused) | |
| 5 | RT | -32767 | **Throttle pedal** | -32767 = released, +32767 = fully pressed. |
| 6 | HAT0X | 0 | **D-pad horizontal** | Three-state: -32767 / 0 / +32767. |
| 7 | HAT0Y | 0 | **D-pad vertical** | Three-state: -32767 / 0 / +32767. |

### Buttons (11 total, indexed 0–10)

Reported via the standard xpad gamepad mapping; the physical label depends on the wheel's face/paddles.

| # | XInput label |
|---|--------------|
| 0 | A |
| 1 | B |
| 2 | X |
| 3 | Y |
| 4 | LB (left shoulder / paddle) |
| 5 | RB (right shoulder / paddle) |
| 6 | View / Select |
| 7 | Menu / Start |
| 8 | Xbox / Home |
| 9 | LSB (left stick press) |
| 10 | RSB (right stick press) |

## Data flow

### Primary: mainboard USB-A → Teensy USB Host

```
Hori wheel --USB--> Mainboard USB-A --> Teensy 4.1 USB Host header
                                            |
                                            v
                            USBHost_t36 JoystickController
                                            |
                                            v
                           onboard LED + logic in kart_controller.ino
```

The firmware instantiates `USBHost`, two `USBHub`s, and a `JoystickController`, polled from `loop()` via `serviceUsbHostWheel()`. On connect it broadcasts `INFO WHEEL_HOST_CONNECTED vid=… pid=… type=…` to both the USB CDC serial and the Pi UART. While any button is held, the Teensy onboard LED (pin 13) lights.

### Fallback: Pi USB → UART → Teensy

```
Hori wheel --USB--> Raspberry Pi --/dev/serial0, 115200 8N1--> Teensy 4.1
                       |                                            ^
                       v                                            |
         wheel_bridge.py reads /dev/input/js0       handles WHEEL_BTN command
```

Use this path when the Teensy USB Host cannot bind the device (e.g. if `USBHost_t36` does not recognize a specific vendor variant). The Teensy ORs both sources, so the onboard LED responds to whichever path is delivering data.

### Line protocol additions

- `WHEEL?` — query host connection + current button state
  - Response: `OK WHEEL host_connected=<0|1> [vid=… pid=… type=…] host_buttons=0x… pi_buttons=0x…`
- `WHEEL_BTN <idx> <0|1>` — button state change (Pi fallback path)
  - Example: `WHEEL_BTN 0 1` / `WHEEL_BTN 0 0`
  - Idempotent; missed edges reconcile on the next change.

## Bring-up

### Mainboard path (preferred)

1. Plug wheel into the mainboard USB-A.
2. Over the Pi UART, send `WHEEL?` and look for `host_connected=1` with the Hori VID/PID (`0x0f0d` / `0x0152`).
3. Press any wheel button — the Teensy onboard LED (pin 13) should light. Watch for `INFO WHEEL_HOST_CONNECTED …` on first enumeration.

### Pi fallback

1. Plug wheel into a Pi USB port. Verify `lsusb | grep -i hori` and `ls /dev/input/js0`.
2. Map inputs: `python3 hardware-scripts/raspberry-pi/wheel_probe.py`. Ctrl+C prints a summary.
3. Run the bridge: `python3 hardware-scripts/raspberry-pi/wheel_bridge.py`. Onboard LED responds as above.
