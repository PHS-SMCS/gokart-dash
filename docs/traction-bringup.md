# Traction Track Bring-up (kart-core, on stands)

Step-by-step for the traction phases T1–T4 (SOFTWARE-STACK-PLAN.md §8). This is
the `kart-core` firmware in `firmware/kart-core`, built with
**`KART_TRACTION_ONLY_BENCH = 1`** (config.h) because the Steervo is away — so
there is **no steering authority**. Valid only with the driven wheels off the
ground. The LED strip is solid **magenta** in SAFE to signal this mode loudly.

> Nothing here spins the motor until **T4**, which is supervised, with the ESC
> powered and Ben at the bench. T1–T3 run with the **ESC unpowered**.

## Build & flash

```bash
pio run -d firmware/kart-core -e teensy41      # -> .pio/build/teensy41/firmware.hex
pio run -d firmware/kart-core -e teensy41 -t upload   # Ben flashes
pio test -d firmware/kart-core -e native       # host logic tests (no hardware)
```

To return to the full-authority build later, set `KART_TRACTION_ONLY_BENCH 0`
in `firmware/kart-core/src/config.h` and reflash.

## Talk to it

Over USB serial (`/dev/ttyACM*`) or the Pi UART (`/dev/serial0`, 115200):

```
PING            -> OK PONG
VERSION         -> OK VERSION kart-core 0.3.2-traction proto=1
STATUS          -> rich gate/health snapshot (see below)
WHEELRAW        -> every wheel axis + button mask (axis/button discovery)
I2C             -> scan the DAC bus (lists ACKing addrs; empty = DAC unpowered)
DACREAD         -> read back the MCP4725 (verify it stored what we wrote)
DACSET <pct>    -> SAFE-only: hold a fixed throttle voltage for metering (10 s)
GEAR <low|med|high> -> resync the open-loop gear model to the FarDriver app
DISARM / FAULT_CLEAR -> stop / clear a latched fault from the Pi side
```

`STATUS` fields used constantly for diagnosis:
`state fault bench wheel thr brk hall hz10 contactor rev speed dac dacok dacfail plaus busrdy vstop thr0 wheelok`.
The gate bits (`dac busrdy thr0 vstop plaus wheelok`, 1 = satisfied) tell you
*why* arming/DRIVE is blocked; `dacok/dacfail` are cumulative DAC-write counters
(watch the deltas to see I2C health under motor load).

> ⚠️ **Hardware reality found during bring-up (corrects the ESC-unpowered
> premise of T1–T3 below):** the throttle DAC is powered from the ESC's **ACC+
> rail, which is gated by the key**. So the DAC only works with the ESC keyed
> on — you *cannot* verify the throttle DAC with the ESC off. `dac_ok` is
> therefore a **DRIVE-entry gate**, not an arm gate, and the real power-up order
> is **arm (contactor closes) → key on (DAC powers) → DRIVE → throttle.** See
> "Bench operations & hard-won lessons" at the bottom — read that first.

## T0 — confirm the wheel axes (do this first)

The pedal axis indices/ranges in `steering-wheel.md` were measured on the Pi's
Linux `js` API. Through the Teensy's `USBHost_t36` Xbox decoder they **may
differ** (triggers are sometimes `0..1023`, not `±32767`). Confirm before
trusting the throttle:

1. Plug the wheel into the mainboard USB-A. Watch for
   `INFO WHEEL_CONNECTED vid=0x0F0D pid=0x0152 type=…`.
2. Run `WHEELRAW` repeatedly while pressing **only the throttle**, then **only
   the brake**, then turning the wheel. Note which `aN` moves for each and its
   released vs. fully-pressed values.
3. If they differ from the defaults (`axis_thr=5`, `axis_brk=4`, `axis_steer=0`,
   released `-32767`, pressed `+32767`), set them live:
   ```
   CFG axis_thr 5
   CFG ped_released -32767
   CFG ped_pressed 32767
   ```
   (RAM only — once confirmed, bake the values into `config.h` so they persist.)

## T1 — throttle DAC (ESC unpowered)

Goal: DAC output (ESC connector pin 27, TPS) tracks the throttle pedal,
0.5 V released → ~4.3 V full, with a visible slew-rate ramp.

1. Put a scope/DMM on the throttle DAC line (relative to throttle ground).
   At boot it should sit at **~0.5 V** (idle floor) in SAFE.
2. **Arm:** hold **both paddles (LB+RB)** + press the **brake** + leave throttle
   released, for **1 second**. LED → amber (ARMED), contactor closes after the
   settle dwell. (ESC unpowered, so closing it is harmless.)
3. **Drive:** press **Menu/Start** with the throttle at zero. LED → green
   (DRIVE).
4. Press the **throttle**: the DAC voltage should rise smoothly (slew-limited,
   ~25 %/s) toward 4.3 V and snap back to 0.5 V instantly on lift.
5. Press the **brake** while on throttle → DAC drops to idle (brake overrides).
6. **Stop:** press **View/Select** (or send `DISARM`). LED → white-ish (back to
   SAFE in normal mode; magenta here in bench mode).

Fault check: unplug the wheel during DRIVE → controlled stop → latched
`WHEEL_LOST` (LED flashing red). Reconnect, `FAULT_CLEAR`, re-arm.

## T2 — ESC discrete lines + contactor (ESC unpowered)

On a DMM at the ESC connector, verify the MOSFET ground lines assert correctly:

- **Contactor (pin 32):** open in SAFE; closes on ARM after the settle dwell;
  opens again on SAFE/FAULT. Precharge is the **always-on external 100 Ω BMS
  resistor** — not Teensy-controlled — so the bus is already charged before you
  ever arm.
- **Brake-low (ESC pin 21):** asserts when the brake pedal passes threshold in
  DRIVE, and throughout a controlled stop.
- **Reverse:** direction is set in the FarDriver app (motor direction); the
  firmware leaves the REV line at its natural sense (X button toggles it).
- **Gear (ESC high-speed line):** this ESC's high-speed input is a momentary
  gear-up-cycle button (1→2→3→1 per pulse). Right paddle = upshift (1 pulse,
  clamps at HIGH), left paddle = downshift (2 pulses = down one in the wrap,
  only above LOW). Gear is tracked open-loop and shown on the LED
  (green/cyan/blue); resync with `GEAR <low|med|high>` if it drifts from the app.

## T3 — hall speed

Spin the rear wheel **by hand**. `STATUS` (`hall=`, `hz10=`) and the telemetry
stream should show pulses and a non-zero frequency. While "moving," DRIVE entry
is blocked (zero-speed is a DRIVE-entry gate) and a controlled stop waits for
zero speed. After ~300 ms with no pulses it reads stopped again.

## T4 — supervised wheel spin ⚡ (Ben + ESC powered, on stands)

Only after T1–T3 pass. ESC powered, contactor live, rear wheel free. Arm →
low-throttle wheel-spin → brake → fault drills (wheel pull, e-stop, implausible
pedal). This is the §8 ⚡ gate and needs Ben's explicit go-ahead.

---

# Bench operations & hard-won lessons

Everything below was learned getting the kart to actually drive. **Read this
before bench work** — much of it is non-obvious and cost real debugging time.

## Driver control scheme (current, hardware-confirmed)

The Hori enumerates through `USBHost_t36` as **Xbox One (joystickType 3)**, and
its button/axis map there is **not** the Linux-`js` map in `steering-wheel.md`.
Confirmed mapping (baked into `src/config.h`):

| Control | Where | Notes |
|---|---|---|
| Throttle | axis **4**, `0…1023` | (not axis 5 / ±32767) |
| Brake | axis **3**, `0…1023` | |
| Steering | axis **0**, `±32767` | unused in traction build |
| Left paddle | button bit **12** | downshift |
| Right paddle | button bit **13** | upshift |
| DRIVE button | button bit **6** | context-sensitive (below) |
| X button | bit **2** | reverse-intent toggle |

- **Arm:** hold **both paddles + brake**, throttle released, ~1 s → amber.
- **DRIVE button (bit 6) is the whole flow:** ARMED→DRIVE, DRIVE→disarm,
  FAULT→clear. (DISARM/FAULT_CLEAR also over UART.)
- **Gears (paddles, while armed/driving):** right = upshift, left = downshift.
  LED in DRIVE shows the gear: **green=LOW, cyan=MED, blue=HIGH**.

## The power-up / drive / shutdown sequence (order matters)

```
SAFE → arm chord → ARMED (contactor closes) → turn ESC KEY ON (DAC powers,
       dac=1) → DRIVE button → DRIVE → throttle.
Shutdown: lift throttle → DRIVE button (disarm) → SAFE → THEN key off.
```

- **Key after contactor** (hardware rule): never key the ESC before the
  contactor is closed. Arming closes the contactor; key on during ARMED.
- DRIVE needs `dac=1` (key on). If you press DRIVE with `dac=0` it just sits in
  ARMED and times out (30 s). `STATUS` shows the gate.
- **Disarm before keying off.** Keying off while driving cuts DAC power; the
  firmware only faults on that if throttle is pressed (it's graceful at a stop),
  but disarm-first is the clean path.

## Flashing workflow (important)

- **CLI flashing works** now (`pio … -t upload`, or the Teensy Loader GUI +
  program button). The Hori wheel enumerates under the PlatformIO build because
  **`patch_usbhost_t36.py`** (a pre-build `extra_scripts` step) injects the
  wheel's VID/PID (`0F0D:0152`) into USBHost_t36's `JoystickController` table at
  build time. Before that fix the bundled USBHost_t36 (v0.2) didn't know the
  wheel, so it only worked when flashed from the Arduino IDE — whose Teensyduino
  shipped a USBHost_t36 that already recognized it. That's why the IDE *used to*
  be required; it no longer is.
- **After any flash, the USB host needs a fresh connect to re-enumerate the
  wheel.** A soft-reboot leaves an already-connected wheel dark, so either
  **hot-plug the wheel** (unplug/replug at the Teensy USB-A host port) or
  power-cycle the board after flashing. Confirm with `WHEELRAW` (`enum=1`,
  `type=3`).
- The program button can be hard to reach in the enclosure; the Teensy Loader
  GUI waits for the press.
- kart-core is a PlatformIO project; the IDE needs a flat sketch. Run
  **`firmware/kart-core/gen_arduino_sketch.sh`** to (re)generate
  `arduino/kart_core/` from the canonical `src/` + `lib/` + `firmware/common`
  (it vendors WDT_T4 and rewrites includes). Open `arduino/kart_core/kart_core.ino`,
  select Teensy 4.1, USB Type Serial, upload. **Never hand-edit `arduino/`.**
- Confirm the flash took with `VERSION` (bump `kVersion` in `main.cpp` each
  change so you can tell). The Teensy is board-powered, so unplugging the USB
  data cable does **not** reboot it.

## Monitoring (how to watch what's happening)

There's no installed pyserial; use uv. Talk to `/dev/ttyACM0` @ 115200:

```bash
uv run --with pyserial python - <<'PY'
import serial,time
s=serial.Serial("/dev/ttyACM0",115200,timeout=0.3); time.sleep(0.2)
s.write(b"STATUS\n"); time.sleep(0.3); print(s.read(s.in_waiting).decode())
PY
```

For live tests, run a **background poller** that prints `STATUS`/`WHEELRAW`
deltas (state, `thr`, `dac`, `dacfail`, `hall`, buttons) — unbuffered
(`python -u`, `flush=True`) writing to a log you `tail`. This is how every drive
test was debugged (which gate blocked, whether DAC writes failed under motor
load, which button bit fired). Watch **`dacfail` deltas during the motor spin**
to gauge I2C noise. Gotchas: the 20 Hz binary telemetry goes to `Serial2`
(Pi UART) **not** USB, so over USB you only get command replies + `INFO` lines;
and never `pkill -f` a pattern that also matches the launching shell (it
self-kills) — use `fuser -k /dev/ttyACM0` to free the port.

## Hardware quirks & gotchas discovered

- **Throttle DAC (MCP4725, header J3, addr 0x60) is powered by ESC ACC+** →
  only alive with the key on. An empty `I2C` scan = key off, not a dead DAC.
- **Motor EMI corrupts the DAC's I2C** once the wheel spins → DAC_ERROR. Each
  write is now read-back-verified (`writeThrottleRaw`) and only faults after
  `kDacFailMs` of continuous failure. Real fix is HW: stronger I2C pull-ups
  (2.2 k), routing SDA/SCL away from motor phases, DAC VCC decoupling. The
  Teensy I2C pins are **3.3 V only (not 5 V tolerant)** — pull-ups must not hard
  pull to 5 V.
- **Hall input (pin 2) picks up noise:** the LED strip's 24 V **PWM** coupled in
  → false motion → blocked arming. Fixed by driving the LEDs **on/off only,
  never PWM**, plus an ISR glitch filter (`kHallMinIntervalUs`). With the ESC
  keyed on, the hall line is also noisy (`vstop` flickers) — harmless for DRIVE
  entry, but it can block FAULT_CLEAR/arming until the wheels truly stop.
- **ESC high-speed line is a momentary gear-cycle button** (1→2→3→1 per
  grounding pulse), not a level select. Firmware pulses it (up=1, down=2) and
  tracks gear **open-loop** — resync with `GEAR` if it drifts.
- **Motor direction + commutation are ESC-side:** a buzzing/vibrating motor =
  hall/phase mapping → fix with the **FarDriver app's motor self-learn**.
  Forward/reverse direction is also set in the FarDriver app.
- **`KART_TRACTION_ONLY_BENCH = 1`** in `config.h` for all of the above
  (stands-only, no steering). Set to `0` for any ground-driving build.
