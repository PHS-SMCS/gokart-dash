# Steering bring-up — Hori wheel → Teensy → CAN → Steervo → PWM → Talon

How to bring the steer-by-wire loop up on the bench. Read
[`SOFTWARE-STACK-PLAN.md`](SOFTWARE-STACK-PLAN.md) §4 and
[`protocols/can-ids.md`](protocols/can-ids.md) first; this is the hands-on
procedure layered on top.

> **Never move a motor without Ben's explicit go-ahead.** First motion is
> supervised, on the bench, with a way to cut power instantly.

## The chain

```
Hori wheel axis ─USB host→ Teensy (kart-core)
   → STEER_SET (angle setpoint, 50 Hz) ─CAN 1 Mbps→ Steervo (ESP32)
   → PID against the steering pot (GPIO32)
   → servo PWM (GPIO25, 1.0/1.5/2.0 ms) → Talon SRX → CIM → gearbox → steering
   → STEER_STATUS (measured angle, faults, 50 Hz) ─CAN→ Teensy   (heartbeat)
```

The Steervo runs the position loop locally; the Teensy only sends a target angle
and watches the heartbeat. The Talon is **PWM-driven, not on CAN** — the only CAN
traffic is the Teensy↔Steervo kart frames.

## Wiring checklist

- **CAN:** Teensy CAN3 (mainboard pins 30/31, via the MCP2562) ↔ Steervo
  transceiver (CTX GPIO21 / CRX GPIO22). **120 Ω at each of the two bus ends.**
- **Steering pot:** wiper → Steervo GPIO32, powered from **3.3 V** (not 5 V —
  ESP32 ADC pins are not 5 V tolerant). Mechanically coupled to the steering so
  it tracks the real angle.
- **Talon PWM:** Steervo **GPIO25** → Talon SRX PWM signal in, plus a common
  ground. The Talon must be in **PWM mode** (no RoboRIO/CAN owner claiming it).
- Confirm the Talon's PWM throw: 1.5 ms should be neutral. If it was last
  calibrated on an FRC robot the endpoints are usually already 1.0/2.0 ms.

## Two safety gates (both must be cleared for motion)

1. **Steervo compile gate** — `kEnableMotorOutput` in `firmware/steervo/src/main.cpp`.
   While `false` the Steervo always commands **neutral PWM** even when fully
   ACTIVE. Leave it `false` for the link/pot/calibration steps; flip to `true`
   and reflash only for the supervised first-motion step.
2. **Teensy runtime gate** — `STEER ON` / `STEER OFF` over the Teensy serial
   (`/dev/ttyACM0` @ 115200). Default **OFF** at boot. This sets the `ENABLE`
   bit in `STEER_SET`. Independent of arm/drive — you can bring steering up
   without arming traction.

## Procedure

Talk to the Teensy with `uv run --with pyserial pyserial-miniterm /dev/ttyACM0 115200`
(or the Steervo's own USB serial for its `INFO` lines).

### 1. Verify the CAN link (no motor)

Flash both firmwares (Steervo with `kEnableMotorOutput = false`). On the Teensy:

```
STEER
```

Expect `link=1` and `sv_state=READY` (or `cal=0` / fault bit
`NOT_CALIBRATED` if never calibrated). `link=0` means no `STEER_STATUS` is
arriving — check CAN wiring, termination, and that both ends are at 1 Mbps.

### 2. Verify the pot

Move the steering by hand and watch `pot=` in repeated `STEER` calls (or the
Steervo's serial). It should sweep smoothly without hitting the rails (≈64…4031
is the plausible window; rail readings are a wiring fault).

### 3. Calibrate (Teensy must be in SAFE)

```
STEER CAL ENTER       # Steervo → CALIBRATING
# center the steering by hand, then:
STEER CAL CENTER
# full left lock, then:
STEER CAL LEFT
# full right lock, then:
STEER CAL RIGHT
STEER CAL SAVE        # commits; persists to ESP32 NVS
STEER                 # expect cal=1, sv_state=READY
```

`STEER CAL ABORT` discards an in-progress calibration. The left/right marks are
assigned ±`kSteerRangeCdeg` (±30.00°); wheel full-lock then commands those same
stops. Calibration survives power cycles (NVS), so this is a one-time step per
mechanical setup.

### 4. Dry-run direction check (motor still disabled)

With `kEnableMotorOutput` still `false` the Steervo computes the PID output but
always commands neutral PWM — so you can confirm the loop *sign* before any
motion. `STEER ON`, then hold the steering a few degrees off the wheel's
commanded angle and read `STEER`:

- `out_pct` should point **toward** closing the error (turn the steering left of
  the target → the PID should command the direction that pushes it right).

If `out_pct` is the wrong sign for the displacement, the feedback is inverted —
fix it now (swap the Talon's two motor leads) rather than discovering it as a
runaway with the motor live. Then `STEER OFF`.

### 5. First motion (SUPERVISED)

1. Set `kEnableMotorOutput = true` in `firmware/steervo/src/main.cpp`, reflash
   the ESP32.
2. **Lower the output limit first** for safety: `STEER CFG LIM 15` (15 %).
3. Be ready to cut power. Then `STEER ON`.
4. Nudge the wheel a few degrees. The motor should drive the steering toward the
   commanded angle and hold. Watch `out_pct` and `meas_cdeg` track `set_cdeg`.

If the motor runs *away* from the target and pins a stop (`out_pct` saturates,
then a `STALL` fault after ~0.8 s), the loop sign is inverted — swap the Talon's
motor leads (the dry run above should have caught this). Do not just raise gain.

`STEER OFF` de-energizes immediately.

### 6. Tune

- `STEER CFG KP <v>` — proportional gain (output fraction per centi-degree;
  default 0.002). Raise until it holds firmly without buzzing/oscillating.
- `STEER CFG KD <v>` — derivative, to damp overshoot.
- `STEER CFG LIM <pct>` — max output %. Raise from 15 % toward the working
  limit (default 40 %) once direction and stability are confirmed.
- `STEER CFG MARGIN <cdeg>` — soft-limit margin inside the calibrated stops.

CFG values are RAM-only on the Steervo (lost on reboot); bake good ones into
`SteerConfig` defaults in `firmware/steervo/lib/steervo/steer_controller.h`.

## Faults & recovery

`STEER` shows `fault_bits` (see `STEER_STATUS` in can-ids.md):

| Bit | Meaning | Clears how |
|---|---|---|
| `POT_RANGE` | pot reading off the rails (broken wire / wiper off track) | **power-cycle** the Steervo (latched hard fault) |
| `STALL` | sustained near-max output with no movement | **power-cycle** (latched) |
| `SETPOINT_STALE` | no fresh `STEER_SET` for 150 ms | resumes when frames return |
| `NOT_CALIBRATED` | no valid calibration | run the calibration sequence |

A `FAULT` heartbeat (or 150 ms with no heartbeat) makes the Teensy treat
steering as failed; in a full-authority build that triggers a controlled stop.
In traction-only bench mode the traction side ignores steering health, so you
can debug steering without it faulting the (stands-only) traction path.

## Notes

- `KART_TRACTION_ONLY_BENCH` (config.h) is about *traction* (stands-only); it
  does **not** disable steering. Steering has its own `STEER ON` gate. For a
  ground-driving build set that flag to `0` so steering health gates DRIVE.
- The Talon self-neutralizes if PWM stops (~100 ms) — a crashed/reset Steervo
  parks the motor. We also command neutral whenever not ACTIVE.
