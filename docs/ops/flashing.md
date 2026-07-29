# Flashing Firmware

Both firmwares are PlatformIO projects. **Flashing is always done by a human**,
never automatically, and always as a deliberate step.

## Toolchain

PlatformIO is invoked through `uv` on the bench machine:

```bash
# Host unit tests (no hardware) — keep these green
pio test -d firmware/kart-core -e native
pio test -d firmware/steervo  -e native

# Build the firmware images
pio run -d firmware/kart-core -e teensy41
pio run -d firmware/steervo  -e esp32dev
```

!!! note "How PlatformIO is run on this bench"
    PlatformIO is not installed as a bare `pio` here; it is run via
    `uvx --with pip platformio …` (the `--with pip` is required, or the ESP32
    toolchain install fails). The commands above are shown with `pio` for brevity.

The safety logic lives in `firmware/*/lib/` as plain C++ with **no Arduino
dependencies**, so it compiles and is unit-tested on the host (`-e native`). CI
builds both firmwares and runs the host tests on every push.

## Flashing the Teensy (`kart-core`)

The Teensy's headless auto-reboot into program mode does not work reliably, so the
GUI loader is used and the **physical PROGRAM button must be pressed**:

1. Build the image (`pio run -e teensy41`).
2. Launch the Teensy loader (`teensy_post_compile … -reboot`).
3. **Press the PROGRAM button** on the Teensy.
4. Confirm with `VERSION` on `/dev/ttyACM0` (e.g. `OK VERSION kart-core 0.5.2-rc`).
5. **Hot-plug the Hori wheel** (or power-cycle the board) so the USB host
   re-enumerates it, then confirm with `WHEELRAW` (`enum=1`).

!!! warning "Regenerate the flat sketch after editing kart-core"
    `kart-core` also ships a flattened Arduino IDE sketch under
    `firmware/kart-core/arduino/`. It is **generated** — never hand-edited. After
    changing the PlatformIO sources, regenerate it with
    `firmware/kart-core/gen_arduino_sketch.sh` so the two stay in sync.

## Flashing the Steervo (ESP32)

The ESP32 flashes over USB with esptool's automatic reset — no button:

```bash
pio run -d firmware/steervo -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

The Steervo prints an `INFO BOOT steervo …` banner and a 10 Hz `STAT` line on its
USB serial once running.

## Ports

| Device | Port | Baud |
|---|---|---|
| Teensy (`kart-core`) | `/dev/ttyACM0` | 115200 |
| Steervo (ESP32) | `/dev/ttyUSB0` | 115200 |

## Build gates (safety flags to check before flashing)

- `firmware/kart-core/src/config.h` → `KART_TRACTION_ONLY_BENCH` — `1` = stands-only
  (no steering DRIVE gate), `0` = full-authority (ground) build.
- `firmware/steervo/src/main.cpp` → `kEnableMotorOutput` — when `false`, the Steervo
  never moves the motor even when ACTIVE (link/pot/calibration-only build).
- Both firmwares must be built from the **same** `KART_CAN_BITRATE` or the CAN link
  will not come up.
