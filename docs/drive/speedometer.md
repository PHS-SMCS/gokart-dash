# Speedometer

The kart measures road speed from a **pulse output on the ESC** that ticks as the
motor turns. The firmware counts pulses, converts to a frequency, and the Pi turns
that into miles per hour for the dashboard [speed dial](../dash/tab-drive.md).

## Which signal — SPD, not "hall pulses"

The ESC exposes two tach-like outputs, and only one of them actually tracks speed:

| ESC signal | Teensy pin | Behavior |
|---|---|---|
| **SPD** — digital speed pulse (ESC pin 13) | **pin 22 (A8)** | **Silent at rest, clean pulses once the wheel turns.** This is the speed source. |
| Hall Pulses (ESC pin 18) | pin 2 | Emits a **fixed ~340 Hz whenever the ESC is energized**, even with the wheel dead still. Does **not** track road speed. Abandoned (kept only for diagnostics). |

!!! note "Why pin 2 was abandoned"
    On the bench (motor on stands, driven at known throttle steps) pin 2 read a
    constant ~340 Hz even at 4–20 % throttle where the motor only whined without
    rotating. A real speed component rides faintly on top once the wheel turns, but
    the fixed 340 Hz swamps it. SPD (pin 22) is silent at rest and switches on with
    rotation — exactly the correlation pin 2 lacks.

### ESC configuration needed for SPD

Out of the box SPD was silent/erratic. Two **FarDriver app** settings fixed it:

- **Speed pulses: 2 → 6** (3× resolution);
- **Speedometer mode: "pulse" → "isolated pulse"** (a clean, isolated output).

These are the `Speed Pulses` and `SpeedoMeter` fields on the ESC's Display page —
see the [FarDriver ESC Settings](../reference/esc-fardriver.md) reference for the
full parameter set (including the wheel-dimension fields that scale the ESC's own
speed/odometer readings).

## How the firmware reads it

- An interrupt on pin 22 counts each SPD edge into `g_spdCount`, glitch-filtered to
  reject sub-interval noise.
- A windowed estimator averages the count over **250 ms** to produce a frequency
  (`hz_x10`), which feeds telemetry and the `vehicle_stopped` flag.
- `vehicle_stopped` — a **safety gate** used for DRIVE-entry and for deciding when
  a controlled stop is complete — is therefore SPD-based, with a conservative **500
  ms** stop timeout so an inter-burst gap is never mistaken for "stopped."

!!! info "Low-speed burstiness is cogging, not noise"
    On stands at a crawl the motor lurches between magnetic detents, so SPD arrives
    in tight bursts with gaps up to ~30 ms. The 250 ms window averages these; the
    500 ms stop timeout keeps a gap from reading as stopped. At real road speed the
    motor turns smoothly and SPD becomes a continuous, countable stream.

## Converting to miles per hour

The Pi converts frequency to speed with a scale factor:

```
mph = SPD_frequency_Hz × HALL_MPH_PER_HZ
```

where `HALL_MPH_PER_HZ` is an environment-tunable constant on the Pi.

!!! warning "The scale factor is a provisional geometry estimate — needs road calibration"
    The default (`≈ 0.057 mph/Hz`) is derived from an **assumed** drivetrain
    reduction, pulses-per-rev, and tire size:

    - drivetrain reduction motor→axle ≈ 8.37 (gearbox × sprocket),
    - **assuming 6 pulses per motor revolution** → ≈ 50 pulses/wheel-rev,
    - a 16″ tire → ≈ 50″ circumference.

    Whether "6 speed pulses" is per motor-rev, per electrical cycle, or per
    wheel-rev is **unconfirmed**, and it changes the factor a lot. Until it is
    calibrated by driving a measured distance (or counting wheel revolutions at a
    steady speed) and setting `HALL_MPH_PER_HZ` accordingly, the dial tracks real
    speed **proportionally** but the magnitude is provisional. See
    [Hardware & Open Items](../hardware-open-items.md).

## Diagnostics

`HALLDIAG [seconds]` streams both tach lines per 100 ms window — `hall_*` (pin 2,
raw), `spd_*` (pin 22), and an analog envelope of pin 22 — so you can see exactly
what each line is doing. `HALLDIAG OFF` stops it.

!!! warning "SPD → pin 22 may be a bench wire"
    The [Mainboard Pin Map](../SMCSKart-Mainboard/README.md) does **not** assign the
    ESC SPD line (pin 13) to a Teensy pin — the SPD → pin 22 connection appears to
    be a direct bench wire. Confirm the physical wiring against the current board.
