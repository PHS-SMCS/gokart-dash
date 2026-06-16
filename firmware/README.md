# Firmware

| Project | Target | Role |
|---|---|---|
| `kart-core/` | Teensy 4.1 | Motion authority: drive state machine, Hori wheel input, throttle DAC, ESC lines, contactor, CAN supervision of Steervo, telemetry to the Pi |
| `steervo/` | ESP32 | Steer-by-wire: pot feedback → PID → Talon SRX over CTRE CAN |
| `common/` | both | Shared wire formats (`kart_can.h`, `crc16.h`) — mirrors [`docs/protocols/`](../docs/protocols/), change the docs first |
| `legacy/` | Teensy 4.1 / ESP32 | Original Arduino-IDE bring-up sketches. **These are what is currently flashed on the kart.** They stay until kart-core/steervo pass their bench gates (plan §8, Phases 1–2). |

## Building

Requires [PlatformIO](https://platformio.org/) (`uv tool install platformio`):

```bash
pio run  -d firmware/kart-core             # Teensy 4.1 image (.pio/build/teensy41/firmware.hex)
pio run  -d firmware/steervo               # ESP32 image     (.pio/build/esp32dev/firmware.bin)
pio test -d firmware/kart-core -e native   # host unit tests, no hardware needed
pio test -d firmware/steervo  -e native
```

CI builds both images and runs all native tests on every push/PR.

## Design rules

- **Safety logic is plain C++ in `lib/`** with no Arduino dependencies, unit
  tested under the native environment. The Arduino `src/main.cpp` is a thin
  I/O shell. Keep it that way: new logic goes in `lib/` with tests.
- **Wire formats live in `firmware/common`** and must match
  `docs/protocols/`. Never hand-roll packing in a sketch.
- **kart-core traction track (T1–T4):** real Hori-wheel input, throttle DAC,
  ESC discrete lines, contactor sequencing, and hall speed are implemented and
  host-tested. The image ships with `KART_TRACTION_ONLY_BENCH = 1`
  (`src/config.h`) — stands-only, **no steering authority**, LED is solid magenta
  — because the Steervo is away. Set it to `0` for any ground-driving build.
  See [`docs/traction-bringup.md`](../docs/traction-bringup.md). steervo still
  compiles with `kEnableMotorOutput = false`. The motor only spins at the
  supervised ⚡ bench gate (T4), never from CI or by default.
- Flashing is always done by a human (Arduino IDE/`pio run -t upload` from a
  workstation), never from CI or the Pi.
