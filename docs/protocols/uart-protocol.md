# Pi ⇄ Teensy UART Protocol — SMCS Kart

**Status: v1 draft — source of truth.** `/dev/serial0` ⇄ Teensy `Serial2`,
115200 8N1. Any change must be made here first, then mirrored in
`firmware/kart-core` and `pi/`.

The link carries two interleaved channels:

1. **ASCII command channel** (Pi → Teensy requests, Teensy → Pi `OK`/`ERR`/`INFO`
   lines) — human-readable, usable from a terminal, backward compatible with
   the legacy bring-up firmware.
2. **Binary telemetry channel** (Teensy → Pi only) — fixed-rate framed packets.

A receiver distinguishes them by the first byte: binary frames always start
with sync byte `0xF7`, which never appears in the ASCII channel (it is not
printable ASCII and kart-core never emits it in text).

---

## 1. ASCII command channel

One command per line (`\n` or `\r` terminated). Every command produces exactly
one terminal line starting with `OK` or `ERR`. Asynchronous `INFO …` lines may
appear at any time and are not replies.

The Pi is **advisory**: commands that could cause motion are either rejected
outside the proper drive state or are *requests* that additionally require
driver confirmation on the wheel.

### Command set (kart-core)

| Command | Reply | Notes |
|---|---|---|
| `PING` | `OK PONG` | |
| `STATUS` | `OK STATUS k=v …` | Human-readable snapshot (telemetry frames are the machine path). |
| `ARM_REQ` | `OK ARM_REQ pending` | Requests SAFE→ARMED; completes only with driver wheel confirmation. Replaces the legacy `ARM <seconds>` (which remains in `firmware/legacy` only). |
| `DISARM` | `OK DISARMED` | Always allowed; forces controlled stop → SAFE. |
| `SAFE` | `OK SAFE` | Alias for DISARM semantics. |
| `FAULT_CLEAR` | `OK` / `ERR FAULT_ACTIVE` | Clears a *latched* fault only after its cause is gone and the kart is stopped. |
| `LED <r> <g> <b>` | `OK LED …` | SAFE state only; otherwise LEDs signal drive state. |
| `STEER_CAL <enter\|center\|left\|right\|save\|abort>` | `OK …` | Forwarded to Steervo (`0x102`), SAFE only. |
| `CFG <name> <value>` / `CFG?` | `OK …` | Bench tuning (slew rates, pedal curve), SAFE only, rejected when armed. |
| `ESC_READ [n]` / `ESC_WRITE <hex>` | `OK …` | ESC serial passthrough for the research track. `ESC_WRITE` is bench-mode only (SAFE + explicit `BENCH on`). |
| `BENCH <on\|off>` | `OK BENCH …` | Enables bench-only commands; refused if hall speed ≠ 0 or state ≠ SAFE. |
| `VERSION` | `OK VERSION kart-core <semver> proto=1` | |

Legacy commands (`THROTTLE`, `BRAKE`, `CONTACTOR`, `SPEED`, `OUTPUT`, …) exist
in the legacy firmware for bring-up via `tools/kartctl.py`; kart-core does
**not** accept direct actuation from the Pi — pedals and the state machine are
the only motion path.

---

## 2. Binary telemetry channel (Teensy → Pi)

### Frame format

```
[0xF7] [LEN] [TYPE] [PAYLOAD …] [CRC16_LO] [CRC16_HI]
```

- `LEN` (uint8): number of bytes from `TYPE` through end of `PAYLOAD`
  (i.e. `1 + payload_size`). Max 64.
- `CRC16`: CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`, no reflection,
  no final XOR) computed over `LEN`, `TYPE`, and `PAYLOAD`. Transmitted
  little-endian.
- Resync: on CRC failure or invalid `LEN`, the receiver discards bytes until
  the next `0xF7`.

All multi-byte payload fields are **little-endian**.

### `TYPE 0x01 — TELEMETRY_V1` (20 Hz, payload 30 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `proto_ver` | = 1 |
| 1 | 1 | `drive_state` | 0 SAFE · 1 ARMED · 2 DRIVE · 3 FAULT |
| 2 | 1 | `fault_code` | 0 = none; see fault table below |
| 3 | 2 | `status_flags` (uint16) | bit0 `WHEEL_CONNECTED` · bit1 `STEER_LINK_OK` · bit2 `STEER_CALIBRATED` · bit3 `ESC_LINK_OK` · bit4 `CONTACTOR_CLOSED` · bit5 `REVERSE` · bit6 `BRAKE_ACTIVE` · bit7 `RC_LINK_UP` · bit8 `BENCH_MODE` · rest reserved |
| 5 | 1 | `throttle_pct` (uint8) | Commanded, post-slew, 0–100 |
| 6 | 1 | `brake_pct` (uint8) | Pedal position 0–100 (output is binary in v1) |
| 7 | 2 | `steer_setpoint_cdeg` (int16) | |
| 9 | 2 | `steer_measured_cdeg` (int16) | From `STEER_STATUS` |
| 11 | 4 | `hall_count` (uint32) | Cumulative pulses since boot |
| 15 | 2 | `hall_hz_x10` (uint16) | Pulse frequency ×10 (speed = f(pulses/rev, wheel ⌀) — computed Pi-side) |
| 17 | 2 | `batt_dv` (uint16) | Battery decivolts. 0 until ESC telemetry lands (Phase 5) |
| 19 | 2 | `batt_da` (int16) | Battery deciamps. 0 until Phase 5 |
| 21 | 2 | `esc_rpm` (int16) | 0 until Phase 5 |
| 23 | 1 | `controller_temp_c` (int8) | −128 = unknown |
| 24 | 1 | `motor_temp_c` (int8) | −128 = unknown |
| 25 | 4 | `uptime_ms` (uint32) | |
| 29 | 1 | `seq` (uint8) | Rolling; lets the Pi measure frame loss |

### `TYPE 0x02 — EVENT` (on occurrence, payload 6 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `event` (1 STATE_CHANGE · 2 FAULT_RAISED · 3 FAULT_CLEARED · 4 WHEEL_CONNECT · 5 WHEEL_DISCONNECT) |
| 1 | 1 | `arg` (new state / fault code / 0) |
| 2 | 4 | `uptime_ms` (uint32) |

Events are also mirrored as `INFO …` text lines for terminal users.

### Reserved types

`0x03` STEER_DETAIL · `0x10` RC_CHANNELS · `0x20` ESC_RAW — defined when needed.

### Versioning rules

Fields within `TELEMETRY_V1` are append-only frozen: never reorder, repurpose,
or resize. A breaking change becomes `TELEMETRY_V2` (`TYPE 0x04`), and the
firmware emits both during a transition window.

---

## 3. Fault codes

| Code | Name | Trigger | Recovery |
|---|---|---|---|
| 1 | `WHEEL_LOST` | USB wheel disconnect / stale reports >100 ms in DRIVE | Reconnect wheel, `FAULT_CLEAR`, re-arm |
| 2 | `STEER_TIMEOUT` | No `STEER_STATUS` for 150 ms | Restore Steervo, clear, re-arm |
| 3 | `STEER_FAULT` | Steervo reports FAULT state | Resolve Steervo fault bits first |
| 4 | `PEDAL_IMPLAUSIBLE` | Pedal out-of-range / contradictory >250 ms | Release pedals, clear |
| 5 | `DAC_ERROR` | MCP4725 I2C write NACK | Hardware check, clear |
| 6 | `ARMED_TIMEOUT` | ARMED with no DRIVE entry for 10 s | Informational; auto-return to SAFE (not latched) |
| 7 | `INTERNAL_WDT` | Loop overrun detected pre-watchdog-reset | Investigate before clearing |
