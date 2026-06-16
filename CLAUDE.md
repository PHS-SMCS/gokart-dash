# gokart-dash — guide for coding agents

Software for the SMCS Robotics **drive-by-wire go-kart**: Teensy 4.1 firmware
(the single motion authority), ESP32 steer-by-wire, Raspberry Pi 4
dashboard/telemetry, a React kiosk, and host tools. The Hori racing wheel is the
driver input; braking is ESC-only; there is a hardware e-stop independent of
software.

The **mainboard PCB is a separate repo** (`PHS-SMCS/SMCSKart-Mainboard`, KiCad).
Its pin map is mirrored into `docs/SMCSKart-Mainboard/` here for software
reference — but that repo is **canonical** for anything hardware/pinout.

## Current status (June 2026)

**The traction track works — the kart drives on stands.** Hori pedals → throttle
DAC → motor, with paddle-shift gears, arm/drive/fault handling, brake override,
and a controlled stop. Firmware: `kart-core 0.3.2-traction`; 50 host tests green;
CI builds both firmwares.

Done: drive state machine, throttle (DAC) path, ESC discrete lines + contactor
sequencing, hall speed, LED state/gear signaling, gear shifting, and the full
UART command/telemetry surface.

Open / next:
- **I2C reliability under motor EMI** — the throttle DAC's I2C goes noisy when
  the motor spins. Firmware read-back-verifies + retries each write; the durable
  fix is hardware (2.2 kΩ pull-ups to 3.3 V, route SDA/SCL away from motor
  phases, DAC VCC decoupling). See traction-bringup.md.
- **T4 fault drills** on stands (wheel-pull, e-stop, implausible pedal).
- **Steering track** — the Steervo CAN-link code exists but is unvalidated; the
  Steervo is away for repair. When it returns, set `KART_TRACTION_ONLY_BENCH = 0`
  and validate (plan phases S2 / I1).
- **Pi/dash + BMS telemetry** tracks (plan §5, Phase B) — not started.

## Read these before starting (in order)

1. **`docs/SOFTWARE-STACK-PLAN.md`** — master plan: architecture, the
   Teensy-is-authority safety model, phased roadmap, ESC research links, open
   questions. Source of truth for *what* and *why*.
2. **`docs/traction-bringup.md`** — how to run/flash/monitor the kart, the driver
   control scheme, and **"Bench operations & hard-won lessons"** (hardware
   quirks, the serial-monitoring workflow, flashing gotchas). Read before
   touching hardware.
3. **`docs/protocols/uart-protocol.md`** + **`can-ids.md`** — Pi↔Teensy UART
   framing/commands and the CAN ID map. **Source of truth: edit these first,
   then mirror in `firmware/`.**
4. **`firmware/README.md`** — firmware layout + design rules (safety logic is
   plain C++ in `lib/`, host-tested; `src/main.cpp` is a thin I/O shell; wire
   formats live in `firmware/common`).
5. **`docs/SMCSKart-Mainboard/README.md`** + **`steering-wheel.md`** — board pin
   map (mirror) and the Hori wheel. NB: the wheel axis/button numbers there are
   Linux-`js` values; the **Teensy `USBHost_t36` map differs** — the confirmed
   map lives in `firmware/kart-core/src/config.h` and traction-bringup.md.

## Non-negotiable conventions

- **Never move a motor without Ben's explicit go-ahead.** Motor-capable tests
  are supervised, on stands, wheels lifted. There is a hardware e-stop.
- **Python: use `uv`** (`uv run --with <pkg> …`, `uv tool …`) — never bare
  `pip`/`venv`. No system pyserial; use `uv run --with pyserial`.
- **Firmware flashing is done by a human (Ben) via the Arduino IDE**, not from CI
  or CLI upload. For kart-core, run `firmware/kart-core/gen_arduino_sketch.sh` to
  regenerate the flat IDE sketch from the PlatformIO sources, then flash
  `firmware/kart-core/arduino/kart_core/`. Never hand-edit `arduino/` (generated).
- **Safety logic goes in `firmware/*/lib/` with host tests** (`pio test -e
  native`), not the Arduino shell. CI builds both firmwares + runs the tests.
- **`firmware/kart-core` ships `KART_TRACTION_ONLY_BENCH = 1`** (stands-only, no
  steering authority) while the Steervo is away. Set `0` in `src/config.h` for
  any ground-driving build.

## Dev environment

Bench (mainboard + Teensy 4.1 + ESP32 + Hori wheel) for interactive tests; the
full kart goes on stands for supervised motor tests. The Raspberry Pi 4 runs the
dashboard (SSH access available). Ben flashes the Teensy/ESP32 — agents produce
firmware, he uploads it. Talk to the Teensy on `/dev/ttyACM0` @ 115200
(`PING`, `STATUS`, `WHEELRAW`, `I2C`, … — see traction-bringup.md).

## Quick commands (run from the repo root)

```bash
pio test -d firmware/kart-core -e native    # host unit tests (no hardware)
pio run  -d firmware/kart-core -e teensy41   # build the Teensy image
pio test -d firmware/steervo  -e native      # steervo host tests
firmware/kart-core/gen_arduino_sketch.sh     # regenerate the IDE sketch
```
