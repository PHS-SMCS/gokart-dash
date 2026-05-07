# Teensy firmware refactor — `hardware-scripts/teensy-4.1/kart_controller/`

**Sub-project 3 of 3** in the codebase refactor. Sibling sub-projects shipped
as separate PRs:
1. Python `hardware-scripts/` consolidation — [PR #1](https://github.com/PHS-SMCS/gokart-dash/pull/1)
2. React/TS dashboard — [PR #2](https://github.com/PHS-SMCS/gokart-dash/pull/2)

## Goal

Split the 925-line `kart_controller.ino` into multiple focused files within
the same sketch folder (the standard Arduino multi-file pattern). Replace the
374-line monolithic `handleCommand` `if/else` cascade with a command table
plus per-command handlers. Eliminate the BRAKE/REVERSE/CONTACTOR-vs-OUTPUT
duplication.

No firmware behavior change — every accepted command and emitted response
line stays byte-identical, including the exact text after `OK`/`ERR` prefixes.

## Why

`kart_controller.ino` is 925 lines doing everything: pin map, hardware
abstractions (DAC throttle, ground-switch outputs, RGB LED PWM), USB host
joystick handling, serial line parser, 17 command handlers, safety-arm state
machine, watchdog. Even a small change requires holding all of this in one
file's scope.

Within `handleCommand`, the dispatch is a 374-line `if/else cmd == "..."`
cascade with three commands (`BRAKE`, `REVERSE`, `CONTACTOR`) sharing one
branch and re-implementing the same parse-on-off + arm-check logic that
already exists in the `OUTPUT` branch's `applyNamedOutput`.

## Out of scope

- **Behavior change.** Every `OK`/`ERR` response line must be byte-identical
  to pre-refactor for the same input.
- **String → char[] migration in the parser hot path.** The current code uses
  Arduino `String` (heap-allocated) for `g_usbRx`, `g_piRx`, `cmd`, `mode`.
  This is a real heap-fragmentation concern in long-running embedded code,
  but rewriting the parser to fixed `char` buffers is a separate behavior-
  sensitive refactor. Listed as a known follow-up at the end of this spec.
- **Wrapping globals in a `KartState` struct.** Mechanical but ~50 call sites;
  no functional benefit; touches every output handler. Defer.
- **Adding new commands** (VERSION, buffer-overflow reporting, etc.). Tier 3
  per the original prioritization; defer.
- **Replacing the Arduino IDE multi-file convention.** Headers are `.h`,
  definitions are `.cpp`, the entry sketch stays `kart_controller.ino` with
  `setup()`/`loop()`. No CMake, no PlatformIO migration.

## Architecture

### File structure (within the sketch folder)

The Arduino IDE / arduino-cli compiles every `.h`/`.cpp`/`.ino` in the sketch
folder as one translation unit per `.cpp`. We split by responsibility:

```
hardware-scripts/teensy-4.1/kart_controller/
├── kart_controller.ino       Entry: setup(), loop(), service*() calls
├── pins.h                    All PIN_* constants + bus baud rates
├── safety.h / safety.cpp     isArmed(), armRemainingMs(), arm-window state,
│                             applySafeState(), serviceArmTimeout()
├── outputs.h / outputs.cpp   setReverse/setBrake/setSpeed*/setCruise/
│                             setContactor + writeThrottleRaw / setThrottlePercent
│                             + LED hardware (writeLedHardware)
├── wheel.h / wheel.cpp       kWheelButtonColors[] + combinedWheelButtons() +
│                             refreshLedFromWheel() + setLed() +
│                             updateOnboardLedFromWheel() +
│                             serviceUsbHostWheel() + USBHost objects
├── parser.h / parser.cpp     parseOnOff, parseFloatStrict, parseUInt32Strict,
│                             hexNibble, parseHexBytes, printHexByte,
│                             requireArmed, broadcastInfo,
│                             plus servicePort() (line-buffer reader)
├── commands.h / commands.cpp Per-command handler functions + ROUTES table +
│                             handleCommand() dispatcher + printHelp +
│                             printStatus
└── state.h / state.cpp       Module-scope shared state (volatile pulse
                              counter, arm-until timestamp, output booleans,
                              throttle %, LED bytes, wheel button mask, USB
                              host objects, RX String buffers, FlexCAN
                              instance)
```

The `state` module owns the formerly-file-scope `g_*` globals so other
modules don't redefine them. `state.cpp` holds the actual definitions;
`state.h` re-declares each as `extern`. Globals remain plain `g_*`
variables — NOT wrapped in a struct, that's deferred. The compile gate
catches multiple-definition errors if a `g_*` is accidentally defined in
both `state.cpp` and a consumer module.

### Command-table dispatch

Today, `handleCommand` is one function with a 17-arm `if/else` over `cmd`.
Refactor:

```cpp
// commands.h
struct CommandHandler {
  const char *name;                                  // uppercase command name
  void (*handle)(int argc, char **argv, Stream &out);
};

extern const CommandHandler kCommands[];
extern const size_t kCommandCount;
```

```cpp
// commands.cpp — per-command handlers
static void cmdPing(int argc, char **argv, Stream &out)   { out.println("OK PONG"); }
static void cmdHelp(int argc, char **argv, Stream &out)   { printHelp(out); }
static void cmdStatus(int argc, char **argv, Stream &out) { printStatus(out); }
static void cmdArm(int argc, char **argv, Stream &out)    { /* ... */ }
// ... 14 more

const CommandHandler kCommands[] = {
  {"PING",       cmdPing},
  {"HELP",       cmdHelp},
  {"STATUS",     cmdStatus},
  {"ARM",        cmdArm},
  {"DISARM",     cmdDisarm},
  {"SAFE",       cmdSafe},
  {"OUTPUT",     cmdOutput},
  {"SPEED",      cmdSpeed},
  {"BRAKE",      cmdBrake},
  {"REVERSE",    cmdReverse},
  {"CONTACTOR",  cmdContactor},
  {"THROTTLE",   cmdThrottle},
  {"LED",        cmdLed},
  {"HALL",       cmdHall},
  {"HALL?",      cmdHall},
  {"ESC_WRITE",  cmdEscWrite},
  {"ESC_READ",   cmdEscRead},
  {"CAN_TX",     cmdCanTx},
  {"CAN_POLL",   cmdCanPoll},
  {"WHEEL",      cmdWheel},
  {"WHEEL?",     cmdWheel},
  {"WHEEL_BTN",  cmdWheelBtn},
};
const size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

void handleCommand(const String &lineIn, Stream &out) {
  // tokenize as today, then table-lookup:
  for (size_t i = 0; i < kCommandCount; ++i) {
    if (strcasecmp(tokens[0], kCommands[i].name) == 0) {
      kCommands[i].handle(n, tokens, out);
      return;
    }
  }
  out.println("ERR UNKNOWN_CMD");
}
```

The `BRAKE`/`REVERSE`/`CONTACTOR` aliases dispatch to handlers that delegate
to the same `applyNamedOutput` path that `OUTPUT brake on` uses today,
eliminating the duplicate parse-and-arm-check code. Wire format unchanged.

### Behavior preservation guarantees

For each command, the per-handler function emits the exact same response
text the original `if/else` branch produces. This is a manual mapping, not
auto-generated, so the migration plan (separate document) walks each command
individually and shows the before/after handler bodies.

The line tokenizer (`String → char buf[192] → strtok`) stays the same. The
arm-check rules stay the same. The output-format functions (`printStatus`,
`printHelp`, etc.) move to `commands.cpp` unchanged.

## Verification

The merge gate, in order:

1. **Pre-refactor compile baseline:** `arduino-cli compile --fqbn teensy:avr:teensy41
   hardware-scripts/teensy-4.1/kart_controller` exits 0 today (already
   confirmed: FLASH 81 KB, RAM1 31 KB used).
2. **Post-refactor compile:** same command exits 0 after every commit.
3. **Memory parity:** post-refactor `FLASH` and `RAM1 variables` usage stays
   within ±5% of baseline. Significant deviation = unexpected bloat;
   investigate.
4. **Wire-protocol regression review:** for each of the 17 commands, manually
   diff the per-handler response text against the original branch. The plan
   document (separate) tabulates this.
5. **Hardware smoke (deferred to deploy-test):** flash the Teensy, run
   `kartctl ping`, `kartctl status`, `kartctl validate bringup`. Capture in
   the PR's test plan; mark "deferred to Pi deploy-test if hardware not
   available at PR time."

The compile + memory gate runs on every commit and catches structural breaks
(missing externs, signature mismatches, multiple-definition errors). The
manual response-text review catches behavioral drift the compiler can't.

## Risks and mitigations

- **R1: Multiple-definition link errors** if the same global is defined in
  two `.cpp` files. Mitigation: every shared variable lives in `state.cpp`
  with the definition; other files include `state.h` which has `extern`.
  The compile-on-every-commit gate catches violations immediately.
- **R2: Arduino IDE auto-prototyping breaks across files.** The Arduino IDE
  inserts forward prototypes for all functions in a single `.ino` file. Once
  functions move to `.h`/`.cpp`, the auto-prototype path is gone — every
  function must be declared in a header before its first use. Mitigation:
  every header explicitly declares its function surface; the compile gate
  enforces this.
- **R3: Behavior drift in command response text.** The most insidious risk —
  the firmware compiles, but a command emits slightly different text. This
  WOULD ship a regression to the dashboard (which parses status responses).
  Mitigation: per-command response-text review against original code in the
  plan; the dashboard's `parse_status` only reads keys (not formatting), so
  small whitespace differences would still parse — but explicit policy is
  byte-identical.
- **R4: USB host + framer-motion-style header dependencies.** `USBHost_t36`
  declares `JoystickController g_joystick(g_usbHost);` style globals that
  must be constructed at file scope. Mitigation: keep these in `wheel.cpp`
  (the only consumer); export only the `serviceUsbHostWheel()` API.
- **R5: Interrupt-handler symbol visibility.** `onHallPulse` is attached via
  `attachInterrupt(...)`. Mitigation: declare in `parser.h` (it touches the
  pulse counter), define in `parser.cpp`. Compile gate catches link errors.

## Deliverables

- 13 new files in `hardware-scripts/teensy-4.1/kart_controller/` (6
  modules with paired `.h`/`.cpp` plus `pins.h` header-only):
  `pins.h`, `safety.{h,cpp}`, `outputs.{h,cpp}`, `wheel.{h,cpp}`,
  `parser.{h,cpp}`, `commands.{h,cpp}`, `state.{h,cpp}`.
- `kart_controller.ino` reduced to: includes for the sub-headers, `setup()`,
  `loop()`, and any glue that doesn't fit elsewhere.
- Per-command handlers in `commands.cpp` consolidating the
  `BRAKE`/`REVERSE`/`CONTACTOR`-vs-`OUTPUT` duplication.
- Documentation: a brief comment block in `kart_controller.ino` pointing to
  each module's responsibility (replaces the implicit organization in the
  current single-file structure).
- No changes to `hardware-scripts/host/`, `hardware-scripts/raspberry-pi/`,
  `src/`, `deploy/`, or any file outside the sketch folder (other than this
  spec doc and its follow-up plan).

## Known follow-ups (deferred from this PR)

These were called out in the cross-layer issue list but are not addressed
here. Each warrants its own spec/plan/PR.

- **String → char[] in the line buffer and tokenizer.** Heap fragmentation
  concern in long-running firmware. Behavior-sensitive parser change; risk
  level too high to bundle with the file split.
- **`KartState` struct wrapping the globals.** Cosmetic; ~50 call sites; no
  behavior change. Cheap follow-up.
- **`VERSION` / `BUILD_ID` command.** Lets the dashboard report which
  firmware build is running.
- **Buffer-overflow reporting in `servicePort`.** Currently silently drops
  characters past the 180-char buffer; could log `INFO LINE_TOO_LONG`.
- **Pre-existing `printHexByte` macro collision** (already fixed in commit
  77c4dd4) — no further action needed.

## Sub-project context

Sub-project 3 of 3. The Python and React PRs do not touch this layer; this
PR does not touch theirs. Each sub-project produces working software on its
own.
