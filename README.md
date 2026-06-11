# SMCS Kart — Software Monorepo

Software stack for the SMCS Robotics go-kart: a drive-by-wire electric kart
built around a custom mainboard ([hardware repo](docs/SMCSKart-Mainboard/README.md))
carrying a Raspberry Pi 4 (dashboard + telemetry) and a Teensy 4.1 (real-time
control), with an ESP32-based steer-by-wire unit ("Steervo") on a shared CAN bus.

**Start here:** [`docs/SOFTWARE-STACK-PLAN.md`](docs/SOFTWARE-STACK-PLAN.md) —
the architecture, safety design, and phased roadmap.

## Repo layout

| Path | What lives here |
|---|---|
| `firmware/kart-core/` | Teensy 4.1 firmware (PlatformIO) — the motion authority: drive state machine, pedal mapping, watchdogs, throttle DAC, ESC lines, CAN to Steervo |
| `firmware/steervo/` | ESP32 firmware (PlatformIO) — steering position loop: pot feedback, PID, CTRE CAN frames to the Talon SRX |
| `firmware/common/` | Headers shared by both firmwares (CAN message definitions, CRC) |
| `firmware/legacy/` | Original bring-up sketches (`kart_controller.ino`, `steer_controller.ino`) — what's flashed today, kept for reference until kart-core/steervo replace them |
| `pi/` | Pi-side Python: hardware probes, `teensy_bridge.py` HTTP service (to become `kartd`) |
| `dash/` | React + TypeScript dashboard (Vite), 800×480 kiosk SPA |
| `tools/` | Operator CLI tools (`kartctl.py`, `can_tool.py`, `esc_tool.py`) for bench bring-up |
| `docs/protocols/` | **Source of truth** for the UART and CAN protocols |
| `docs/SMCSKart-Mainboard/` | Hardware reference: pin map, ESC connector, steering wheel |
| `deploy/` | systemd units, udev rules, Cage kiosk launcher ([guide](deploy/README.md)) |

## Architecture (one screen)

```
 Hori wheel ──USB──► Teensy 4.1 (kart-core, AUTHORITY) ──I2C──► MCP4725 ──► ESC throttle
 CRSF RX ──Serial3─►    │   │ GPIO ──► ESC brake/REV/speed/contactor   │
                        │   └─Serial1──► FarDriver ND721000 (telemetry)│
            CAN 1 Mbps  │ (11-bit IDs: Teensy↔Steervo · 29-bit: CTRE)  │
                        ▼                                              ▼
                  Steervo ESP32 ──CTRE CAN──► Talon SRX ──► CIM ──► steering
                        ▲ pot (GPIO 32, 3.3 V)
 Pi 4 ◄──UART /dev/serial0 115200──► Teensy   (Pi = dashboard/telemetry only,
   └─ kartd + kiosk dashboard (:5173/:5174)    never in the motion path)
```

The Teensy is the single motion authority. The Pi and dashboard are advisory:
nothing they send can move the kart, and they can crash without affecting
driving. Safety design, failure-mode table, and the drive state machine are in
the [plan](docs/SOFTWARE-STACK-PLAN.md).

## Quick start

### Dashboard on a workstation

```bash
cd dash
npm install
npm run dev          # http://localhost:5173 with HMR
```

Views render with mock telemetry when no bridge is present.

### Firmware

Both firmware projects build with [PlatformIO](https://platformio.org/):

```bash
pio run  -d firmware/kart-core            # Teensy 4.1 image
pio run  -d firmware/steervo              # ESP32 image
pio test -d firmware/kart-core -e native  # host-side unit tests (no hardware)
pio test -d firmware/steervo  -e native
```

Flashing is done by a human, never automatically.

### Bench bring-up

See [`docs/bringup.md`](docs/bringup.md) — safety checklist first, then Pi
probes, firmware upload, and `kartctl` smoke tests.

### Pi kiosk deployment

See [`deploy/README.md`](deploy/README.md) for the from-zero Pi OS Lite install.

## Safety

- The firmware enforces an arm/disarm gate on every output that can move the
  kart. The dashboard only surfaces state; it cannot arm by itself.
- **Never run anything that can spin a motor (drive or steering) without the
  bench supervisor's explicit go-ahead.** Lift the wheels, follow
  [`docs/bringup.md`](docs/bringup.md).
- The kart has **no friction brake** — braking is ESC motor braking,
  commanded by software. Treat every change to `firmware/` as safety-critical
  and keep the host-side unit tests green.

## License

MIT.
