# Closed-Loop Pot Control

The Steervo holds the steering at the commanded angle with a **closed-loop
position controller**: it reads the true steering angle from a potentiometer,
compares it to the setpoint from the Teensy, and drives the motor to close the
error. The control core (`firmware/steervo/lib/steervo/steer_controller.*`) is
pure C++ under unit test; the ESP32 sketch wires it to the ADC, PWM, and NVS.

## The feedback potentiometer

- **Wiper → ESP32 GPIO36** (SENSOR_VP, input-only, ADC1 channel 0).
- **Powered from 3.3 V** — *not* 5 V. The ESP32's ADC pins are not 5 V tolerant;
  feeding the pot from 5 V would damage the pin at one end of travel.
- Mechanically coupled through the **50:1 gearbox**, so it tracks the real output
  steering angle and encodes total motor travel. It is therefore also the
  **end-stop guard** — never let the motor drive it off either end.
- Read at 12-bit resolution, **16× oversampled** (with a throwaway first sample) to
  tame the ESP32 SAR ADC's noise.

## Calibration

The pot's raw ADC value has no inherent meaning until it is calibrated to physical
angles. Calibration is **guided over CAN** (the `STEER_CAL` message) and stored in
the ESP32's **NVS** flash, so it survives power cycles and is a one-time step per
mechanical setup:

```mermaid
flowchart LR
    enter["ENTER<br/>→ CALIBRATING"] --> center["center by hand<br/>→ MARK_CENTER"]
    center --> left["full left lock<br/>→ MARK_LEFT"]
    left --> right["full right lock<br/>→ MARK_RIGHT"]
    right --> save["SAVE_EXIT<br/>→ persist to NVS, READY"]
```

`SAVE_EXIT` assigns the captured left/right stops to **±30.00°** (`±kSteerRangeCdeg`)
and commits; `ABORT` discards. The motor never runs while CALIBRATING. From the
Teensy this is driven with `STEER CAL ENTER / CENTER / LEFT / RIGHT / SAVE / ABORT`
(the kart-core must be in SAFE to send them).

## The controller: pure proportional

After a long tuning effort, the shipped controller is **pure proportional**:

| Gain | Value | Note |
|---|---|---|
| `Kp` | 0.002 | output fraction per centi-degree of error |
| `Ki` | 0 | — |
| `Kd` | **0** | deliberately zero |
| output limit | 0.4 (40 %) | conservative clamp on Talon command |

This is stable, accurate (~0.15° steady-state), and oscillation-free even at full
output — **the 50:1 gearbox's own friction supplies the damping** a derivative term
would normally provide.

!!! danger "Leave `Kd` at 0"
    The pot jitters ~±12 cdeg at rest. Differentiating that over a 10 ms tick
    injects ~±1000–2000 cdeg/s of *noise* velocity, so any real `Kd` saturates the
    output and drives a rail-to-rail limit cycle — that was the entire "oscillation"
    saga. The PID (`pid.h`) uses derivative-on-**measurement** plus a low-pass
    filter, so it *can* accept a `Kd` if you ever add a cleaner velocity source, but
    with the raw pot, don't.

The setpoint is clamped to the calibrated range **minus a soft-limit margin**, so
the controller never targets the mechanical stop itself.

## Live tuning

Over CAN (`STEER_CFG`, driven from the Teensy as `STEER CFG …`) you can adjust
`KP`, `KI`, `KD`, the output limit (`LIM`), and the soft-limit margin (`MARGIN`)
**while READY or CALIBRATING — never while ACTIVE**. These are RAM-only on the
Steervo (lost on reboot); good values are baked into the `SteerConfig` defaults in
`firmware/steervo/lib/steervo/steer_controller.h`.

## Wheel → motion direction

Which way a given input deflection turns the steering is a **Teensy-side** flag,
`kSteerInvertDirection` in `firmware/kart-core/src/config.h` (currently `true` on
this build — a left wheel turn drove the steering right without it). It is
independent of the control-loop sign (motor lead polarity) and the pot calibration;
flip only this if the **command sense** is backwards. If the motor *runs away* from
the target instead, that is the loop sign — swap the Talon's motor leads, don't
flip this flag.

!!! warning "Steering mechanicals not in the repo"
    The gearbox ratio (documented as 50:1), the CIM motor, and the pot's mounting
    and total mechanical travel are physical facts. Also note the pot pin: the
    firmware and current bring-up notes use **GPIO36**, while some older docs say
    GPIO32 — confirm the physical wiring. See
    [Hardware & Open Items](../hardware-open-items.md).
