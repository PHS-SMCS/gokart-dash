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
| Steering | **Steervo**: ESP32 → **PWM** → Talon SRX → CIM motor via gearbox; potentiometer on ESP32 GPIO 32 for angle feedback. |
| CAN topology | **1 Mbps bus, Teensy↔ESP32 only** (11-bit standard IDs). The Talon is PWM-driven (Steervo GPIO25), off-bus. (Earlier plan ran the Talon over CTRE 29-bit extended IDs on this bus; retired in favor of PWM.) |
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
                   REV, speed sel,      │  PWM ──────────► Talon │
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
   │        ARMED ────────────►│   precharge then main contactor closed,
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
- **ARMED entry sequence (precharge):** the traction pack contactor has a
  precharge resistor (confirmed). On ARM the Teensy asserts the precharge path,
  dwells until the ESC bus is charged (conservative fixed dwell by default;
  until a bus-voltage sense crosses threshold if that line is wired), then
  closes the main contactor and releases precharge. A failed precharge (dwell
  elapses with no charge / no bus-voltage rise) aborts to SAFE with a fault.
  See §3.5.
- **DRIVE entry conditions:** wheel connected, throttle pedal at zero, hall
  speed = 0, **and** — in the normal driving configuration — Steervo heartbeat
  healthy and calibrated. The steering-health condition is the *only* thing the
  **traction-only bench mode** (§3.6) relaxes, and only with the kart on stands.
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
| Steervo heartbeat loss | No `STEER_STATUS` for 150 ms | Fault → controlled stop (steering holds last position, motor de-energized by Steervo's own timeout). **Suppressed in traction-only bench mode** (§3.6): steering is declared absent, not a fault. |
| Steervo reports fault (stall, pot out of range) | `STEER_STATUS` fault bits | Fault → controlled stop. Suppressed in traction-only bench mode. |
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

### 3.5 Contactor / precharge sequencing

The traction contactor has a precharge resistor, so closing it is a two-step
sequence, not a single line toggle:

1. **Assert precharge** — energize the precharge path so the resistor charges
   the ESC's bus capacitance through a current limit.
2. **Dwell** — wait for the bus to charge. Default is a conservative fixed
   dwell (tunable; start generous, e.g. ~1–2 s). If a bus-voltage sense line is
   available, prefer waiting until it crosses a charged threshold, with the
   fixed dwell as an upper-bound timeout.
3. **Close main contactor**, then **release precharge**.

A precharge that never completes (dwell elapses with the bus still low, where
sensed) is a fault → abort to SAFE; never close the main contactor onto an
uncharged bus. On any controlled stop / SAFE transition both the main contactor
and precharge open. **Hardware detail to confirm:** whether precharge is a
separate Teensy-driven line or a passive/automatic timer relay — code the
precharge output as a configurable pin and fall back to the timed dwell if it
is automatic (see §9).

### 3.6 Traction-only bench mode (Steervo absent)

A **stands-only** configuration that lets the rear-motor/traction path be
brought up and tested while the Steervo is unavailable (e.g., away for repair).
It is the kart-core analog of Steervo's `kEnableMotorOutput` gate and follows
the same discipline:

- **Explicit, deliberate gate.** A compile-time flag (e.g.
  `kTractionOnlyBenchMode`) — never the default build, never shipped in the
  first-drive image. The normal driving configuration always keeps the full
  steering-health DRIVE-entry gate.
- **What it changes:** removes *only* the "Steervo heartbeat healthy and
  calibrated" DRIVE-entry condition and suppresses the two Steervo watchdog
  faults (§3.3). No `STEER_SET` enable is ever emitted. Everything else — pedal
  plausibility, throttle slew, precharge sequence, hall-speed-zero entry, brake
  override, controlled-stop on every other fault — is unchanged.
- **Stands-only invariant.** Valid only with the driven wheels off the ground;
  there is no steering authority in this mode. The dash and LED strip must
  signal the mode loudly (it is **not** a driving state).
- **Reverting is a one-line change + reflash**, so the build that ever touches
  the ground is unambiguously the full-authority one.

---

## 4. Steervo firmware — `steervo`

PlatformIO ESP32 project. Replaces the hello-world `steer_controller.ino`.
Bring-up procedure: [`steering-bringup.md`](steering-bringup.md).

- **Talon PWM driver:** standard servo PWM on GPIO25 (50 Hz; 1.0 ms full
  reverse / 1.5 ms neutral / 2.0 ms full forward) via the ESP32 LEDC peripheral.
  The Talon must be in PWM mode (no CAN/RoboRIO owner). It self-neutralizes if
  pulses stop (~100 ms) — a *free* hardware failsafe under ESP32 crash; we also
  command neutral whenever not ACTIVE. (The earlier CTRE-CAN driver, ported from
  [willGuimont/CanControl](https://github.com/willGuimont/CanControl), is retired
  but survives as `ctre_frames.h` for reference.)
- **Position loop:** pot on GPIO 32 (12-bit ADC) → PID to angle setpoint.
  Conservative output clamp (default 40 %); a compile gate (`kEnableMotorOutput`)
  forces neutral until supervised first motion.
- **Safety local to Steervo:**
  - Soft limits from calibration (never command past pot min/max margins).
  - Pot plausibility: out-of-range or frozen-while-driving ⇒ fault, motor off.
  - Stall detection: sustained max output with no pot movement ⇒ fault.
  - **Setpoint staleness: no `STEER_SET` for 150 ms ⇒ motor off + fault bit.**
- **Calibration mode:** guided center/end-stop calibration commanded over CAN,
  results stored in NVS.

### CAN message set (11-bit standard IDs; full spec in `protocols/can-ids.md`)

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
- **BMS reader (battery monitoring):** the pack uses a **JKBMS
  JK-B2A24S20P** (8S–24S, 200 A, active balance) with RS485 **and** Bluetooth.
  So pack/cell voltages, current, and temps have a dedicated source independent
  of the ESC. v1 reads the BMS over **RS485** (USB-RS485 adapter on the Pi)
  using the community-documented JK protocol, merges pack V / current / min-max
  cell / temps into the telemetry stream, and feeds the dash battery gauge.
  This is non-motion-critical (same class as ESC telemetry — the Teensy never
  depends on it). Bluetooth is a later alternative transport; not needed for
  v1. Build as a standalone `pi/bms_probe.py` first (read + decode + print),
  then fold into `kartd`.
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

### Two parallel tracks

Steering and traction are independent below the drive state machine, so the
roadmap is split into a **Steering track** (needs the Steervo + Talon hardware)
and a **Traction track** (rear ESC/motor, contactor, BMS). The Steervo is away
for hardware repair, so:

- **Everything except the steering *hardware* gates can be finished now.** A
  coding agent can complete the Traction track, the Pi/dash track, and all
  shared software — including writing the Steervo CAN-link *code* — without the
  Steervo present.
- **Only two gates genuinely block on the Steervo's return:** Phase 1's
  supervised steering motor test (S2) and the steering-under-load portion of
  full integration (I1). Those are flagged 🔒 below.
- Traction bring-up on stands runs under **traction-only bench mode** (§3.6),
  which removes the steering-health DRIVE-entry gate — stands-only, behind an
  explicit flag, never in a ground-driving build.

### Shared foundation (done / do now)

| Phase | Scope | Exit gate (motors off unless noted) |
|---|---|---|
| **0. Foundations** ✅ | Repo restructure, PlatformIO scaffolds, protocol spec docs, CI, host test harness | Both firmwares build in CI; protocol docs reviewed |
| **K. kart-core core (shared)** | Drive state machine, watchdogs, ARM chord, UART telemetry stream, LED states — host-tested; pinned SAFE on bench, no motor outputs applied | Host tests green; scaffold stays SAFE on bench; telemetry frames parse on the Pi |

### Steering track (CODE now; hardware gates 🔒 wait for Steervo)

| Phase | Scope | Exit gate |
|---|---|---|
| **1. Steervo bench (code)** ✅ | CTRE CAN driver, pot map, PID, guided calibration + NVS, fault logic | Host tests green (37 cases) |
| **S1. kart-core ⇄ Steervo CAN link** | FlexCAN @ 1 Mbps, Hori LX → `STEER_SET` at 50 Hz (**`ENABLE` defaults off**), `STEER_STATUS` decode + heartbeat watchdog, `STEER_CAL`/`STEER_CFG` in SAFE. Degrades gracefully when no Steervo answers (declares steering absent; in normal mode that blocks DRIVE, in traction-only mode it's ignored) | Code builds + host-tested; on a 1 Mbps bench bus with *any* CAN peer the frames are well-formed (sniffer-verified). **Live Steervo validation deferred to S2.** |
| **S2. Steervo supervised motor test** 🔒 | Closes Phase 1 against the real Steervo via S1: closed-loop to setpoint, stall + staleness failsafes | ⚡🔒 *When Steervo returns:* supervised closed-loop + failsafe demonstration. Requires flipping `STEER_SET.ENABLE` and Steervo's `kEnableMotorOutput` — Ben's explicit go-ahead |

### Traction track (Steervo not required; kart on stands)

> **Status (June 2026): T1–T4 complete — the kart drives on stands**
> (`kart-core 0.3.2-traction`). Practical notes superseded a couple of premises
> below: the throttle DAC is powered from the ESC's ACC+ (key) rail so it can't
> be verified "ESC unpowered", and the ESC speed input is a gear-cycle button.
> See `docs/traction-bringup.md` and `CLAUDE.md`. Remaining: I2C-under-EMI
> hardening (HW), T4 fault drills.

| Phase | Scope | Exit gate |
|---|---|---|
| **T1. Throttle DAC** | Hori RT → deadband → curve → slew-rate limiter → MCP4725; I2C NACK = fault | **ESC unpowered.** DAC voltage tracks pedal on scope/multimeter; slew limit observed; NACK faults correctly |
| **T2. ESC discrete lines + contactor/precharge** | Brake-low line, REV, speed-select outputs; ARMED-entry precharge sequence (§3.5: precharge → dwell → main contactor → release) | **ESC unpowered.** Line states correct on multimeter; precharge/contactor sequence and timing verified; failed-precharge aborts to SAFE |
| **T3. Hall speed** | Hall pulse capture → speed; pulses-per-rev + wheel diameter (Q3) → mph | Speed reads correctly turning the wheel by hand; zero-speed gates DRIVE entry |
| **T4. Traction-only stands bring-up** ⚡ | **traction-only bench mode** (§3.6); ESC powered, contactor live, rear motor | ⚡ *Supervised, on stands:* arm (precharge→contactor), low-throttle wheel-spin, braking, fault drills (wheel pull, e-stop, implausible pedal) — **no steering required** |

### Pi / dash / telemetry track (DO NOW — parallel)

| Phase | Scope | Exit gate |
|---|---|---|
| **3. Pi + dash** | `kartd` (WebSocket telemetry, blackbox logger), dashboard real data + fault UX | Dash shows live bench telemetry end-to-end; logger captures a session |
| **B. BMS battery telemetry** | JKBMS JK-B2A24S20P over RS485 → `pi/bms_probe.py` → `kartd` → dash battery gauge (Bluetooth later, §5.1) | Pack V / current / min-max cell / temps on the dash; logged |

### Integration & drive (Steervo back on the kart)

| Phase | Scope | Exit gate |
|---|---|---|
| **I1. Full stands integration** 🔒 | Both tracks together on stands: steering under load alongside traction | ⚡🔒 *When Steervo returns:* full arm/drive, **steering under load**, every fault drill (wheel pull, Steervo kill, e-stop) |
| **5. ESC serial + proportional brake** | Telemetry parser, regen research (§7) | ESC telemetry on dash; braking approach selected and validated on stands |
| **6. First drive** | **Restore the full-authority build** (traction-only bench mode off, steering-health DRIVE gate active); drive-readiness checklist, conservative limits, driver briefing | ⚡ *First supervised drive* |
| **later** | RC kill → RC drive (protocol hooks already present), camera view, maps, tuning | — |

> **Agent build order while the Steervo is away:** `K → (T1 → T2 → T3 → T4)`
> for traction, `3 → B` for telemetry, and `S1` for the steering link in code —
> in any interleaving. Stop at the 🔒 gates (**S2**, **I1**) and the ⚡ motor
> tests, which need the Steervo hardware and/or Ben at the bench. The legacy
> firmware cannot drive the new 1 Mbps bus (it runs 500 kbps and speaks only
> the old hello protocol), so any on-hardware CAN check uses the new kart-core
> image or the `firmware/tools/can_sniffer` helper.

---

## 9. Open questions

**Answered:**

- ✅ **Contactor/precharge** — the contactor circuit **has a precharge
  resistor**. ARMED entry is the two-step precharge sequence in §3.5; never
  close the main contactor onto an uncharged bus.
- ✅ **Battery monitoring** — separate sensing exists: a **JKBMS
  JK-B2A24S20P** (RS485 + Bluetooth). v1 reads it over RS485 on the Pi (§5.1,
  Phase B); the battery gauge does not depend on ESC telemetry. Bluetooth is a
  later transport.

**Still open (non-blocking):**

1. **Talon device ID** — what CAN device ID the Talon is configured as. Needed
   for S2 (the steering motor test), discoverable on the bench with
   `firmware/tools/can_sniffer` once the Talon is on a healthy 1 Mbps bus. Does
   **not** block the Traction track.
2. **Hall pulses-per-rev and wheel diameter** — needed to convert hall counts
   to mph (Phase T3 / dash). Mechanically measurable.
3. ~~**Precharge control topology**~~ — **RESOLVED (July 2026)**: precharge is a
   **Teensy-driven line on GPIO27** (HIGH = resistor energized), retrofitted
   after a missing-precharge incident welded the previous contactor shut.
   Firmware sequences it explicitly: precharge 2 s → resistor off → close
   contactor → settle → `bus_ready` (§3.5, `ContactorSequencer`). The resistor
   is thermally limited, so the sequencer enforces a 3 s hard cap and a 10 s
   inter-cycle cooldown. **Still open:** no **bus-voltage sense** line is wired,
   so charge confirmation is the fixed dwell (`has_bus_sense` support exists in
   the sequencer for when one is added).
