# Hardware Script Pack (SMCSKart Mainboard)

This folder contains a bring-up and control script pack derived from `docs/SMCSKart-Mainboard/README.md`, organized by target hardware:

- `raspberry-pi/` → on-board diagnostics and interface probes
- `teensy-4.1/` → firmware command router + safety interlocks
- `host/` → operator-facing serial tools (`kartctl`, CAN/ESC helpers)

## Safety First

These scripts are designed for **bench bring-up first**.

1. Lift driven wheels and secure chassis before any live test.
2. Keep HV/traction disconnected unless explicitly testing propulsion.
3. Treat ESC lines as **active-low control semantics**: user command `on` means “assert/ground ESC line” through MOSFET switching.
4. Start with dry-run and validation:

```bash
python3 hardware-scripts/host/kartctl.py --dry-run validate bringup --profile bench
python3 hardware-scripts/host/kartctl.py --port /dev/ttyACM0 validate bringup --profile bench
```

## Quick Start

### 1) Raspberry Pi bring-up

```bash
cd hardware-scripts/raspberry-pi
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

python3 i2c_scan.py --bus 1 --strict
python3 imu_probe.py --bus 1 --samples 3
python3 gps_probe.py --mode i2c --bus 1
python3 teensy_uart_probe.py --device /dev/serial0 --baud 115200
```

### 2) Upload Teensy firmware

Open `hardware-scripts/teensy-4.1/kart_controller.ino` in Arduino IDE + Teensyduino and upload to Teensy 4.1.

### 3) Host control CLI

```bash
python3 hardware-scripts/host/kartctl.py --port /dev/ttyACM0 ping
python3 hardware-scripts/host/kartctl.py --port /dev/ttyACM0 status
python3 hardware-scripts/host/kartctl.py --port /dev/ttyACM0 --arm-seconds 2 throttle --percent 5
python3 hardware-scripts/host/kartctl.py --port /dev/ttyACM0 safe
```

## Included Scripts

| Target | Script | Purpose |
|---|---|---|
| Pi | `i2c_scan.py` | I2C bus scan with expected-device checks |
| Pi | `imu_probe.py` | MPU6050 ID + accel/gyro sample readout |
| Pi | `gps_probe.py` | NEO-M9N probe over I2C or serial |
| Pi | `teensy_uart_probe.py` | Pi↔Teensy UART ping/safe test |
| Teensy | `kart_controller.ino` | Serial command surface + output guards |
| Host | `kartctl.py` | Main safe control/diagnostic CLI |
| Host | `can_tool.py` | Focused CAN send/poll helper |
| Host | `esc_tool.py` | ESC serial passthrough helper |

For per-target details, see each subfolder README.
