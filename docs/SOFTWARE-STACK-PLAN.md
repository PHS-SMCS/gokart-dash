# SMCS Kart — Software Stack Plan

*Drafted 2026-06-11. Target milestone: **first safe drive** (driver-only, kart on stands first).*

This plan covers the complete software stack across the Teensy 4.1 mainboard, the
Steervo ESP32, the Raspberry Pi 4, and the dashboard. Existing code in
`gokart-dash/` is treated as example/prototype material: protocols and structure
are redesigned where needed, but proven pieces (kiosk deployment, arm-gate
concept, CRSF parser, bridge auto-reopen pattern) are carried forward.

---

## 1. Confirmed system facts and decisions

| Topic | Decision / fact |
|---|---|
| Driver input | Hori Racing Wheel Overdrive on the **Teensy USB host** port — wheel axis = steering, pedals = throttle/brake. Full drive-by-wire. |
| Steering | **Steervo**: ESP32 → Talon SRX → CIM motor via gearbox; potentiometer on ESP32 GPIO 32 for angle feedback. |
| CAN topology | **Single shared 1 Mbps bus**: CTRE protocol to the Talon (29-bit extended IDs) + Teensy↔ESP32 custom messages (11-bit standard IDs). No ID-space collision. |
| Braking | **ESC-only** (no friction brake). Binary low-brake line today; goal is proportional braking via the FarDriver serial protocol (reverse-engineered) or PWM experiments on the brake line. |
| E-stop | A physical e-stop exists independent of software. |
| RC (CRSF/ELRS) | v1 is **driver-only**. Protocol leaves room for remote-kill and remote-drive later. |
| Compute | Raspberry Pi 4 (dashboard, telemetry, logging — **never in the motion-critical path**). |
| ESC | FarDriver ND721000; no official serial docs — research + reverse-engineering track (see §7). |

### Hardware action items (blockers found during review — not software)

1. **Steervo potentiometer must be powered from 3.3 V, not 5 V.** The wiring doc
   feeds the pot from 5 V with the wiper on GPIO 32; ESP32 ADC pins are 3.3 V
   max and will be damaged at one end of travel.
2. **Talon SRX may need FRC-unlocking** if it was ever connected to a RoboRIO:
   power it while holding its reset button ~5 s until the LED blinks green,
   otherwise it ignores non-FRC CAN frames.
3. **CAN termination:** with the bus at 1 Mbps, verify exactly two 120 Ω
   terminations (mainboard end + far end of the bus).

---

## 2. Architecture overview

```
                         ┌────────────────────────────────────────────┐
                         │ Raspberry Pi 4 — UI & telemetry (advisory) │
                         │  kartd (Python): UART link, WebSocket,     │
                         │  GPS/IMU readers, blackbox logger          │
                         │  Dashboard SPA (React kiosk, 800×480)      │
                         └───────────────▲────────────────────────────┘
                                         │ UART /dev/serial0 115200
                                         │ (framed protocol, §6.1)
 Hori wheel ──USB──► ┌───────────────────┴───────────────────┐
 (steer/throttle/    │ Teensy 4.1 — kart-core (AUTHORITY)    │
  brake/buttons)     │  drive state machine · pedal mapping  │
                     │  watchdogs · fault latching           │
 CRSF RX ──Serial3─► │  throttle DAC · brake line · contactor│
 (future kill/drive) │  hall speed · ESC serial · LED strip  │
                     └───────┬───────────────────────┬───────┘
                 I2C: MCP4725│            CAN 1 Mbps │ (shared bus)
                 Serial1: ESC│                       │
                             ▼                       ▼
                  FarDriver ND721000    ┌────────────────────────┐
                  (TPS, low-brake,      │ Steervo ESP32          │
                   REV, speed sel,      │  CTRE frames ──► Talon │
                   serial telemetry)    │  pot feedback, PID,    │
                                        │  soft limits, faults   │
                                        └────────────────────────┘
```

**Authority hierarchy (safety invariant):** the Teensy is the single source of
motion authority. The Pi can *request* (e.g., LED color, view telemetry) but
nothing the Pi sends can produce motion unless the Teensy's state machine is in
DRIVE with a healthy driver-input chain. A crashed/absent Pi never affects
driving. The Steervo never moves the steering motor except on fresh, valid
setpoints from the Teensy.

---

## 3. Teensy firmware — `kart-core`

The safety-critical component. Replaces `kart_controller.ino`. Structured as a
PlatformIO project with the control logic in plain C++ modules that compile on
host for unit tests (the Arduino layer is a thin shell).

### 3.1 Drive state machine

```
            power-on
               │
               ▼
   ┌──────► SAFE ─────────────┐   all outputs off, contactor open,
   │           │ ARM request   │   throttle DAC at 0.5 V
   │           ▼               │
   │        ARMED ────────────►│   contactor closed, outputs still neutral;
   │           │ DRIVE request │   10 s timeout back to SAFE if not driven
   │           ▼               │
   │        DRIVE              │   pedals/steering live
   │           │ fault         │
   │           ▼               │
   └──────  FAULT (latched) ◄──┘   controlled stop, then contactor open;
            requires explicit driver clear + SAFE re-entry
```

- **ARM request** = deliberate driver action on the wheel (e.g., hold both
  shoulder paddles 1 s with brake pedal pressed and throttle released) **or**
  dashboard arm button + wheel confirmation. Never dashboard alone.
- **DRIVE entry conditions:** wheel connected, Steervo heartbeat healthy and
  calibrated, throttle pedal at zero, hall speed = 0.
- **Any fault in DRIVE** triggers a *controlled stop*: throttle → 0, brake
  asserted, then contactor opens once hall pulses indicate stopped (or after a
  hard 3 s cap). Faults latch with a code shown on dash + LED strip color.

### 3.2 Control loops (all in a fixed-rate 100 Hz tick, no blocking I/O)

| Function | Behavior |
|---|---|
| Throttle | Hori RT axis → deadband → curve (gentle initial map) → **slew-rate limiter** (configurable %/s, conservative default) → MCP4725. DAC writes verified; I2C NACK = fault. |
| Brake | Hori LT axis → threshold → ESC low-brake line (v1). Proportional path added in Phase 5 (§7). Brake always overrides throttle (both pedals = brake). |
| Steering | Hori LX axis → calibrated range → angle setpoint → CAN `STEER_SET` at 50 Hz. Steervo runs the position loop locally. |
| Reverse / speed-profile | Wheel button selections, only honored at standstill; REV + speed-select lines driven accordingly. |
| LED strip | State signaling: SAFE=breathing white, ARMED=amber, DRIVE=green, FAULT=flashing red. Manual colors allowed only in SAFE. |

### 3.3 Watchdogs and failure handling

| Failure | Detection | Response |
|---|---|---|
| Wheel USB disconnect / stale reports | USBHost state + 100 ms report timeout in DRIVE | Fault → controlled stop |
| Steervo heartbeat loss | No `STEER_STATUS` for 150 ms | Fault → controlled stop (steering holds last position, motor de-energized by Steervo's own timeout) |
| Steervo reports fault (stall, pot out of range) | `STEER_STATUS` fault bits | Fault → controlled stop |
| Teensy hang | Hardware watchdog (WDOG1), 200 ms | Reset → boots into SAFE (all outputs deterministically off in early init, as today) |
| Pi/UART loss | Informational only | Warning on LED; driving unaffected |
| ESC serial telemetry loss | Stale-data flag | Warning; driving unaffected (v1 doesn't depend on it) |
| Implausible pedal (both extremes, OOR) | Range checks | Treat as brake; fault if persistent |

### 3.4 Pi-facing protocol

Keeps the line-based human-readable command channel (great for bring-up and
`kartctl`), and adds a **binary telemetry stream**: a 20 Hz framed packet (sync
+ length + CRC16) carrying state, speeds, pedal/steering values, fault codes,
Steervo angle, ESC telemetry. Commands and telemetry are interleaved on the
same UART; frames are distinguishable from text lines by the sync byte. Full
spec lives in `docs/protocols/` as the single source of truth, with packing
code generated/shared between firmware and Pi.

---

## 4. Steervo firmware — `steervo`

PlatformIO ESP32 project. Replaces the hello-world `steer_controller.ino`.

- **CTRE CAN driver:** TWAI at 1 Mbps; implements the reverse-engineered
  Phoenix frames — periodic enable/heartbeat frame plus percent-output control
  frame to the Talon's device ID, both at the rates the Talon expects (~every
  50 ms minimum; we'll run 20 Hz enable / 50 Hz control). Port/adapt
  [willGuimont/CanControl](https://github.com/willGuimont/CanControl).
  If the Talon stops receiving enables it disables itself within ~100 ms —
  this is a *free* hardware-level failsafe under ESP32 crash.
- **Position loop:** pot on GPIO 32 (oversampled + median-filtered ADC) → PID
  to angle setpoint. Conservative output clamp initially.
- **Safety local to Steervo:**
  - Soft limits from calibration (never command past pot min/max margins).
  - Pot plausibility: out-of-range or frozen-while-driving ⇒ fault, motor off.
  - Stall detection: sustained max output with no pot movement ⇒ fault.
  - **Setpoint staleness: no `STEER_SET` for 150 ms ⇒ motor off + fault bit.**
- **Calibration mode:** guided center/end-stop calibration commanded over CAN,
  results stored in NVS.

### CAN message set (11-bit standard IDs, coexists with CTRE extended IDs)

| ID | Dir | Payload |
|---|---|---|
| `0x100` `STEER_SET` | Teensy→ESP32 | angle setpoint (int16 centi-deg), seq, flags (enable bit) |
| `0x101` `STEER_STATUS` | ESP32→Teensy | measured angle, output %, fault bits, seq echo — 50 Hz heartbeat |
| `0x102` `STEER_CAL` | Teensy→ESP32 | calibration commands |
| `0x103` `STEER_CFG` | Teensy→ESP32 | PID/limit tuning (bench only, ignored in DRIVE) |

---

## 5. Raspberry Pi — `kartd` + dashboard

### 5.1 `kartd` (Python, systemd service — evolves `teensy_bridge.py`)

One daemon owning `/dev/serial0`, replacing the request/response-only bridge:

- **Telemetry hub:** parses the binary telemetry stream; serves it to the
  dashboard over **WebSocket** (`ws://127.0.0.1:5174/telemetry`) at 20 Hz.
- **Command API:** HTTP POST endpoints for non-critical commands (LED, view
  config) and *requests* (arm request — which the Teensy only honors with
  wheel confirmation). Same localhost-only model as today.
- **GPS + IMU readers:** carried over from the existing bridge (NEO-M9N via
  I2C DDC, MPU6050) — merged into the telemetry stream.
- **Blackbox logger:** every telemetry frame + every state/fault transition to
  disk (SQLite or newline-JSON, rotated). This is the tuning and
  incident-review record; cheap to build now, invaluable on day one of drives.
- **Health:** systemd watchdog (`WatchdogSec`), auto-restart; restart is
  invisible to driving by design.

### 5.2 Dashboard

The existing React kiosk stack (Cage/Chromium, 800×480, views/dock structure)
is good — keep it. Changes:

- `useTelemetry` switches from mock sine waves to the WebSocket feed (the hook
  interface was designed for this).
- **Drive view:** speed (hall-derived), drive state (SAFE/ARMED/DRIVE/FAULT —
  large and unambiguous), steering angle, throttle/brake bars, battery/temps
  once ESC telemetry lands.
- **Fault surfacing:** full-screen banner on FAULT with the fault code and
  recovery instructions; ARMED state visually loud (amber).
- **System view:** subsystem health (wheel, Steervo, ESC link, GPS, RC),
  versions, log markers.
- Lights/Map/Camera views stay as-is and remain non-critical.

---

## 6. Repository & toolchain

- **`gokart-dash` becomes the software monorepo** (it already contains
  firmware + Pi + UI), restructured:

  ```
  firmware/kart-core/      # Teensy 4.1 (PlatformIO)
  firmware/steervo/        # ESP32 (PlatformIO)
  pi/kartd/                # Python daemon + probes
  dash/                    # React SPA (current src/ moves here)
  tools/                   # kartctl, esc_sniffer, can_tool (host CLIs)
  docs/protocols/          # UART framing + CAN ID map (source of truth)
  deploy/                  # systemd units, kiosk scripts (as today)
  ```

- **PlatformIO** for both firmwares: CLI builds, pinned library versions, and
  **host-native unit tests** for the safety logic (state machine, slew
  limiter, CRSF/CAN parsers, plausibility checks) so safety code is tested
  without hardware. Ben flashes the resulting binaries.
- CI (GitHub Actions): build both firmwares, run host tests, lint/build dash.

---

## 7. ESC research track — FarDriver serial + proportional braking

Runs in parallel; v1 driving does **not** depend on it.

1. **Telemetry first (low risk):** implement a read-only parser using the
   community protocol docs
  ([jackhumbert/fardriver-controllers](https://github.com/jackhumbert/fardriver-controllers),
  [Endless Sphere thread](https://endless-sphere.com/sphere/threads/fardriver-controller-serial-protocol-reverse-engineering.121825/)) —
  16-byte CRC16-protected status frames rotating through controller memory:
  RPM, battery V/A, controller & motor temps, fault codes. Validate against
  the bench ESC with an `esc_sniffer` tool (Teensy passthrough → Pi capture).
2. **Proportional braking research:** in priority order —
   a. **Regen level via serial:** the protocol docs cover writing config
      registers; if regen strength is settable live, map brake pedal →
      stepped regen levels. Test on stands.
   b. **PWM on the low-brake line:** experiment whether the ESC tolerates a
      duty-cycled brake input as pseudo-proportional braking (plausible but
      unproven — strictly a supervised bench experiment).
   c. Fallback: well-tuned **threshold braking** (two-stage: light regen via
      throttle-cut, full brake line past a pedal threshold).
3. Any write to the ESC over serial happens only in bench mode, never in DRIVE,
   until the command surface is proven.

---

## 8. Phased roadmap

Each phase ends with a bench validation gate. **Nothing that can spin a motor
runs without Ben's explicit go-ahead.**

| Phase | Scope | Exit gate (motors stay off unless noted) |
|---|---|---|
| **0. Foundations** | Repo restructure, PlatformIO scaffolds, protocol spec docs, CI, host test harness | Both firmwares build in CI; protocol docs reviewed |
| **1. Steervo bench** | CTRE CAN driver, pot read (after 3.3 V rewire), PID loop, calibration, fault logic | Talon hello on bench; pot values sane; ⚡*supervised motor test: closed-loop to setpoint, stall + staleness failsafes demonstrated* |
| **2. kart-core core** | State machine, wheel input, watchdogs, DAC throttle (scope/multimeter verification — ESC unpowered), CAN link to Steervo, UART telemetry stream | Host tests green; DAC voltage tracks pedal on bench; fault injection (unplug wheel, kill Steervo) produces controlled-stop behavior |
| **3. Pi + dash** | `kartd` (WebSocket telemetry, blackbox), dashboard real data + fault UX | Dash shows live bench telemetry end-to-end; logger captures a session |
| **4. Stands integration** | Full kart on stands: contactor, ESC powered, all subsystems | ⚡*Supervised: arm sequence, low-throttle wheel-spin, braking, steering under load, every fault drill (wheel pull, Steervo kill, e-stop)* |
| **5. ESC serial + proportional brake** | Telemetry parser, regen research (§7) | ESC telemetry on dash; braking approach selected and validated on stands |
| **6. First drive** | Drive-readiness checklist, conservative limits (low speed profile, throttle cap), driver briefing | ⚡*First supervised drive* |
| **later** | RC kill → RC drive (protocol hooks already present), camera view, maps, tuning | — |

---

## 9. Open questions (non-blocking, answer when convenient)

1. **Talon device ID** — what CAN device ID is the Talon configured as? (Phase 1 needs it; discoverable on the bench if unknown.)
2. **Contactor/precharge** — does the contactor circuit have a precharge resistor, or does the ESC tolerate direct contactor closure? (Affects the ARMED-entry sequence timing.)
3. **Hall pulses-per-rev and wheel diameter** — needed to convert hall counts to mph for the dash.
4. **Battery monitoring** — is pack voltage/current only visible via ESC telemetry, or is there separate sensing? (Determines when the battery gauge becomes real.)
