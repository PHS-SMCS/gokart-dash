# Speedometer — SPD pulse source (bench bring-up, July 2026)

The dashboard speed reads the **ESC SPD "digital speed pulse" output**
(ESC pin 13 "Light-Blue" → **Teensy pin 22 / A8**), **not** the pin-2 "hall
pulses" line. This was settled on the bench by driving the motor at known
throttle steps (wheels lifted) and streaming both lines with `HALLDIAG`.

## Why not pin 2 (ESC pin 18 "Hall Pulses")

Pin 2 emits a **fixed ~340 Hz signal (1947/3895 µs, a 1:2 pattern) whenever the
ESC is energized** — present even with the wheel *dead still* (4–20 % throttle
produced only winding whine, no rotation, yet pin 2 read 340 Hz). It goes to 0
only when the ESC key is off. A real speed component rides faintly on top once
the wheel turns (its per-window *max* crept to ~410 Hz), but the fixed 340 Hz
swamps it. **Pin 2 does not track road speed. Abandoned.** Its instrumentation
is retained in `HALLDIAG` (`hall_*` fields) for reference only.

## Why SPD (pin 22) works — with the right ESC config

Out of the box SPD was silent/erratic. Two FarDriver settings fixed it:
- **Speed pulses: 2 → 6** (3× resolution).
- **Speedometer mode: "pulse" → "isolated pulse"** (clean, isolated output).

After that, with the motor actually rotating (≥~35 % on stands), SPD is a
**clean signal**: `dropped=0` (no EMI/sub-120 µs noise), consistent `spd_min_us`
≈ 1154 µs. It is **silent at rest** and switches on with rotation — exactly the
correlation pin 2 lacks. The wire is direct (ESC-13 → pin-22, no RC/pullup in
the harness); the Teensy provides an internal `INPUT_PULLUP`.

**Low-speed burstiness is cogging, not signal quality.** On stands at a crawl the
motor lurches between detents, so SPD arrives in packets (tight bursts ~1154 µs
apart) with gaps up to ~30 ms. At real road speed the motor turns smoothly and
SPD becomes a continuous, countable stream. `kHallWindowMs=250` averages the
bursts; `kHallStopTimeoutMs=500` keeps an inter-burst gap from being mistaken
for "stopped".

## Firmware wiring (`0.4.15-spdspeedo`+)

- `kPinSpd = 22`, `spdIsr` counts `g_spdCount` (glitch-filtered by
  `kHallMinIntervalUs`), same as the pin-2 hall ISR.
- The speed estimator `g_hall` reads **`g_spdCount`** (was `g_hallCount`). It
  drives `hz_x10`, `vehicle_stopped`, telemetry `hall_count`/`hall_hz_x10`, and
  STATUS `hall=`/`hz10=`.
- `vehicle_stopped` (a safety gate for DRIVE-entry + controlled-stop completion)
  is therefore SPD-based, with the conservative 500 ms timeout above.

## Scale factor (NEEDS road calibration)

`mph = SPD_hz × (wheel_circumference) / (SPD pulses per wheel revolution)`.

Provisional geometry estimate (Pi `HALL_MPH_PER_HZ`, env-tunable):
- Drivetrain reduction motor→axle = 2.37 (gearbox) × 60/17 (sprocket) = **8.37**.
- Assuming **6 pulses/motor-rev** → 6 × 8.37 ≈ **50.2 pulses/wheel-rev**.
- 16" tyre → circumference ≈ 50.3".
- `50.27 / 50.2 × 3600 / 63360` ≈ **0.057 mph/Hz** → default on the Pi.

**This is unverified** — "6 speed pulses" may be per motor-rev, per electrical
cycle, or per wheel-rev; the ratio changes the factor a lot. Calibrate by
driving a measured distance (or counting wheel revs at a steady speed) and set
`HALL_MPH_PER_HZ` on the Pi accordingly. Until then the dial tracks real speed
proportionally but the magnitude is provisional.

## Diagnosing again

`HALLDIAG [secs]` streams both pins per 100 ms window: `hall_*` (pin 2, raw),
`spd_*` (pin 22), plus `adc_*` (a pin-22 analog envelope — confirmed *not*
analog: pinned ~1023 at idle). `HALLDIAG OFF` stops it.
