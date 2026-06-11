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
- **Phase 0 gates:** the kart-core shell pins itself in SAFE (stub inputs),
  and steervo compiles with `kEnableMotorOutput = false` so even a fully
  active controller commands zero demand. These gates come off only at the
  supervised bench tests in Phases 1–2.
- Flashing is always done by a human (Arduino IDE/`pio run -t upload` from a
  workstation), never from CI or the Pi.
