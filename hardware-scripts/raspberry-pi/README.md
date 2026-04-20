# Raspberry Pi Bring-up Scripts

Diagnostics for the Pi-side interfaces documented in the SMCSKart mainboard docs.

## Scope

- I2C scan and expected device checks (`MPU6050`, optional `NEO-M9N` DDC)
- MPU6050 identity + sample readout
- NEO-M9N probe over I2C and/or serial NMEA
- Pi UART link probe to Teensy (`PING` / optional `SAFE`)
- USB steering wheel probe + Teensy bridge

These scripts are read-only/low-risk diagnostics and are intended to run first during bring-up.

## Install

```bash
cd hardware-scripts/raspberry-pi
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Prerequisites

- Raspberry Pi with I2C enabled (`/dev/i2c-1`)
- Optional UART enabled for Teensy serial bridge testing (`/dev/serial0` or `/dev/ttyAMA0`)
- GPS serial device path if probing via UART (board/config dependent)

## Usage

### 1) Scan I2C bus

```bash
python3 i2c_scan.py --bus 1 --strict
```

- Defaults to requiring `0x68` (MPU6050).
- Add required addresses with repeated `--require`.

Example:
```bash
python3 i2c_scan.py --require 0x68 --require 0x42 --strict
```

### 2) Probe MPU6050

```bash
python3 imu_probe.py --bus 1 --address 0x68 --samples 5 --interval 0.05
```

### 3) Probe NEO-M9N GPS

I2C mode:
```bash
python3 gps_probe.py --mode i2c --bus 1 --i2c-address 0x42
```

Serial mode:
```bash
python3 gps_probe.py --mode serial --serial-device /dev/ttyACM0 --baud 9600
```

Auto mode (tries I2C first, then serial if provided):
```bash
python3 gps_probe.py --mode auto --serial-device /dev/ttyACM0
```

### 4) Probe Pi↔Teensy UART link

```bash
python3 teensy_uart_probe.py --device /dev/serial0 --baud 115200
python3 teensy_uart_probe.py --device /dev/serial0 --baud 115200 --safe
```

`--safe` sends `SAFE` after successful ping.

### 5) Map USB steering wheel inputs

```bash
python3 wheel_probe.py --device /dev/input/js0
```

Move every control (wheel, pedals, D-pad) and press every button. Ctrl+C prints a summary of observed axes/buttons. See `docs/SMCSKart-Mainboard/steering-wheel.md` for the expected mapping.

### 6) Forward wheel buttons to Teensy (onboard LED bring-up)

```bash
python3 wheel_bridge.py --wheel /dev/input/js0 --serial /dev/serial0
```

Forwards each button press/release as `WHEEL_BTN <idx> <0|1>` to the Teensy. With the updated firmware, the Teensy onboard LED (pin 13) lights while any wheel button is held.

## Exit Codes

- `0`: success
- `1`: communication/probe failure
- `2`: strict expectation failure (e.g., required I2C address missing)
