# CAN Bus Protocol — SMCS Kart

**Status: v1 draft — source of truth.** Any change to IDs or payloads must be
made here first, then mirrored in `firmware/common/kart_can.h`.

## Bus configuration

| Property | Value |
|---|---|
| Bitrate | **1 Mbps** |
| Termination | 120 Ω at exactly two physical bus ends |
| Nodes | Teensy 4.1 (kart-core), Steervo ESP32 |
| Traffic | **11-bit standard IDs** (this document) |

The Talon SRX is **not on the CAN bus**: the Steervo drives it directly with a
servo PWM signal (1.0 ms full reverse / 1.5 ms neutral / 2.0 ms full forward on
Steervo GPIO25). So the only CAN traffic is the Teensy↔Steervo kart messages
below. (Earlier drafts ran the Talon over CTRE 29-bit extended IDs on this same
bus; that path is retired but the frame builders survive in
`firmware/steervo/lib/steervo/ctre_frames.h` as a reference, ported from
[CanControl](https://github.com/willGuimont/CanControl).) Keep custom IDs in the
ranges below.

## ID allocation

| Range | Use |
|---|---|
| `0x100–0x10F` | Steering (Teensy ⇄ Steervo) |
| `0x110–0x11F` | Reserved: future RC/aux nodes |
| `0x120–0x1FF` | Reserved |

All multi-byte fields are **little-endian**. Angles are **centi-degrees**
(`int16`, +100 = 1.00° right of center).

## Messages

### `0x100 STEER_SET` — Teensy → Steervo, 50 Hz, DLC 4

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `flags` | bit0 `ENABLE` (1 = motor may be energized). All other bits reserved, must be 0. |
| 1 | 2 | `setpoint_cdeg` (int16) | Target steering angle. Ignored when `ENABLE`=0. |
| 3 | 1 | `seq` (uint8) | Rolling counter, +1 per frame. |

**Staleness rule (safety-critical):** if the Steervo receives no valid
`STEER_SET` with `ENABLE`=1 for **150 ms**, it de-energizes the motor, sets
fault bit `SETPOINT_STALE`, and stays in READY until fresh frames resume.

### `0x101 STEER_STATUS` — Steervo → Teensy, 50 Hz heartbeat, DLC 8

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `state` | 0 INIT · 1 READY · 2 ACTIVE · 3 FAULT · 4 CALIBRATING |
| 1 | 1 | `fault_bits` | bit0 `POT_RANGE` · bit1 `POT_FROZEN` · bit2 `STALL` · bit3 `SETPOINT_STALE` · bit4 `TALON_LOST` · bit5 `NOT_CALIBRATED` |
| 2 | 2 | `measured_cdeg` (int16) | Current angle from the pot (calibrated). |
| 4 | 1 | `output_pct` (int8) | Motor command, −100…+100. |
| 5 | 1 | `seq_echo` (uint8) | `seq` of the last accepted `STEER_SET`. |
| 6 | 2 | `pot_raw` (uint16) | Raw filtered ADC value (diagnostics/calibration). |

**Heartbeat rule (safety-critical):** the Teensy treats no `STEER_STATUS` for
**150 ms** — or any frame with `state`=FAULT — as a steering fault and
performs a controlled stop.

### `0x102 STEER_CAL` — Teensy → Steervo, on demand, DLC 2

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `cmd` | 0 ENTER · 1 MARK_CENTER · 2 MARK_LEFT · 3 MARK_RIGHT · 4 SAVE_EXIT · 5 ABORT |
| 1 | 1 | reserved (0) | |

Calibration commands are accepted only while the kart-core reports it is in
SAFE (the Teensy must not send them otherwise). Results persist in ESP32 NVS.

### `0x103 STEER_CFG` — Teensy → Steervo, bench only, DLC 8

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `param` | 1 KP · 2 KI · 3 KD · 4 OUTPUT_LIMIT_PCT · 5 SOFT_LIMIT_MARGIN_CDEG |
| 1 | 4 | `value` (float32 LE) | |
| 5 | 3 | reserved (0) | |

Ignored unless Steervo `state` is READY or CALIBRATING (never ACTIVE).

## Rates and timeouts summary

| Link | Nominal rate | Timeout | On timeout |
|---|---|---|---|
| `STEER_SET` (Teensy→ESP32) | 50 Hz | 150 ms | Steervo: motor off + fault bit |
| `STEER_STATUS` (ESP32→Teensy) | 50 Hz | 150 ms | Teensy: controlled stop |
| Talon PWM (ESP32→Talon) | 50 Hz | ~100 ms (Talon-internal) | Talon self-neutralizes if pulses stop (hardware failsafe). Steervo also commands neutral (1.5 ms) whenever it is not ACTIVE. |
