# Python refactor — `hardware-scripts/`

**Sub-project 1 of 3** in the codebase refactor. Sibling specs will follow for
the React/TS dashboard and the Teensy firmware.

## Goal

Eliminate the three independent implementations of the kart line protocol,
extract repeated utility helpers, and clean up the bridge HTTP dispatcher —
without changing any externally observable behavior (CLI output, bridge JSON
shapes, exit codes).

## Why

Today the same "write line, read until OK/ERR or timeout" loop exists in three
places with subtly different error semantics:

| Implementation                                  | Lock | ERR handling           |
|-------------------------------------------------|------|------------------------|
| `host/serial_link.py` — `KartLink.command`      | no   | raises `KartProtocolError` |
| `raspberry-pi/teensy_bridge.py` — `TeensyLink`  | yes  | returns line as-is     |
| `raspberry-pi/teensy_uart_probe.py` — `send_command` | no | returns line as-is |

`normalize_hex_bytes` is duplicated verbatim across `kartctl.py`, `esc_tool.py`,
`can_tool.py`. The Linux-joystick event format is duplicated across
`wheel_bridge.py` and `wheel_probe.py`. `_try_import_serial` boilerplate is
duplicated across multiple Pi probes.

A single shared module makes future endpoint additions one-stop and guarantees
the bridge and CLIs handle protocol edge cases identically.

## Out of scope

- Tests (no test infra exists in the repo; introducing it is its own project).
- Any behavior change. CLI output, error messages, exit codes, and bridge JSON
  shapes must match pre-refactor output byte-for-byte where the same input is
  given.
- New bridge endpoints. (The route-table refactor enables them, but adding
  routes is a follow-up.)
- Type-checker adoption (mypy/pyright). Inconsistent type hints stay
  inconsistent for now.

## Architecture

### New shared package

Create `hardware-scripts/kart_link/` as a sibling to `host/` and
`raspberry-pi/`:

```
hardware-scripts/
├── kart_link/
│   ├── __init__.py        # re-exports the public surface
│   ├── transport.py       # KartLink class — thread-safe Serial wrapper with
│   │                      #   send_line / command / read_available methods,
│   │                      #   plus auto-reopen
│   ├── protocol.py        # pure parsers: parse_status (others live on the class)
│   ├── errors.py          # KartConnectionError, KartProtocolError, KartTimeoutError
│   ├── hex.py             # normalize_hex_bytes
│   └── joystick.py        # JS_EVENT_* constants, parse_event()
├── host/                  # existing operator CLIs, now consume kart_link
└── raspberry-pi/          # existing Pi services, now consume kart_link
```

### Importing without a packaging step

The Pi runs scripts directly from systemd; introducing `pip install -e` would
add an install step to deploy. Each consumer instead adds two lines at the
top:

```python
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from kart_link import KartLink, KartProtocolError, normalize_hex_bytes  # etc.
```

`Path(__file__).resolve()` makes this cwd-independent so the systemd units (which
launch with their own working directory) keep working. We localize the
boilerplate to one helper if it grows past the trivial.

**Open question for review:** if you'd rather a `pyproject.toml` and an
editable install step in `deploy/README.md`, say so and I'll switch. Default is
the no-install path above.

### Two-tier API on `KartLink`

The bridge and CLIs disagree on what to do with `ERR ...` responses, so the
link offers both modes:

```python
class KartLink:
    def __init__(self, port, baud=115200, timeout=1.5): ...
    def open(self): ...
    def close(self): ...
    # Context manager, is_open, etc.

    # Low-level: returns the OK/ERR line as-is. Used by bridge + probes.
    def send_line(self, command: str, timeout: float | None = None) -> str: ...

    # High-level: wraps send_line, raises KartProtocolError on ERR. Used by CLIs.
    def command(self, command: str, timeout: float | None = None) -> CommandResult: ...

    # Drain pending serial input for a fixed window. Existing behavior.
    def read_available(self, duration_s: float = 0.5) -> list[str]: ...
```

`KartLink` always holds a `threading.Lock` around `send_line` so the bridge
gets thread safety for free; CLIs pay a negligible uncontended-lock cost.

`CommandResult` keeps its existing dataclass shape (`command`, `response`,
`trace`) so callers don't change.

### Bridge route-table dispatch

Replace the `if self.path == "..."` chains in `teensy_bridge.py Handler` with
a small registry:

```python
ROUTES: dict[tuple[str, str], Callable[[Handler], None]] = {
    ("GET",  "/api/health"): _handle_health,
    ("GET",  "/api/status"): _handle_status,
    ("POST", "/api/led"):    _handle_led,
}

def do_GET(self):    self._dispatch("GET")
def do_POST(self):   self._dispatch("POST")
```

Each handler is a small function taking the request handler instance. Adding
a new endpoint becomes one entry in `ROUTES` plus the handler — no editing
the dispatcher.

## Migration plan (per-file)

1. Add the new `kart_link/` modules. No callers yet.
2. Switch `host/kartctl.py`, `host/esc_tool.py`, `host/can_tool.py` to import
   from `kart_link` (transport + errors + `normalize_hex_bytes`). Delete their
   local hex helper.
3. Delete `host/serial_link.py` (its surface now lives in `kart_link`).
4. Switch `raspberry-pi/teensy_bridge.py` to use `KartLink.send_line` in place
   of the local `TeensyLink`. Convert `Handler` to the route-table dispatch.
5. Switch `raspberry-pi/teensy_uart_probe.py` to use `KartLink.send_line`.
   Becomes ~15 lines.
6. Switch `raspberry-pi/wheel_bridge.py` and `wheel_probe.py` to import
   `JS_EVENT_*` constants and parse helper from `kart_link.joystick`.
7. Remove the `_try_import_serial` boilerplate from probes that now go through
   `KartLink` (they get the import error message from the package).

## Verification (no test infra → manual + static)

Each step below must pass before the refactor is considered done.

- **Static:** `python -m py_compile $(git ls-files '*.py')` returns 0.
- **CLI parity (no hardware required):** for each CLI tool, run `--dry-run`
  against every subcommand on the pre-refactor and post-refactor versions.
  `diff` of the captured stdout and exit codes must be empty.
- **Bridge live parity (hardware required):** with the bridge running against
  a Teensy, hit `/api/health`, `/api/status`, `/api/led` and confirm JSON
  shapes match pre-refactor. Field values can differ run-to-run (live data),
  but field names, types, and presence must match.
- **Bridge concurrency smoke (hardware required):** fire concurrent POSTs at
  `/api/led` and verify the Teensy reports the last-sent color cleanly via
  `/api/status` (no interleaved/garbled OK lines on the serial console).
- **Wheel probe parity (hardware required):** `wheel_probe.py` against the
  Hori wheel produces the same event log lines as pre-refactor.

CLI parity is the hard gate: it runs anywhere and catches the bulk of
regressions. The hardware-dependent checks happen on the Pi during
deploy-test; if hardware is unavailable, document which checks were deferred
in the PR and complete them before merge.

## Risks and mitigations

- **R1: Systemd unit launches the bridge with a working directory not equal to
  `raspberry-pi/`.** The `Path(__file__).resolve().parent.parent` boilerplate
  is cwd-independent, so this is safe — but verify by inspecting
  `deploy/gokart-bridge.service` for `WorkingDirectory=` and confirming the
  import still resolves.
- **R2: Bridge thread-lock semantics regress.** The new `KartLink` keeps an
  always-on lock; same lock model as the existing `TeensyLink`. The smoke test
  above is the regression check.
- **R3: CLI's `KartProtocolError` raised at a different point.** `command()`
  now raises on ERR detected by `send_line` — same observable behavior as
  today. CLI parity diff catches divergence.
- **R4: `host/serial_link.py` deletion breaks an out-of-tree consumer I'm not
  aware of.** Mitigation: grep the repo for `from serial_link` and `import
  serial_link` before deletion. If anything outside `host/` imports it, leave
  a 4-line shim that re-exports from `kart_link`.

## Deliverables

- `hardware-scripts/kart_link/` with the modules listed above.
- Updated consumers under `host/` and `raspberry-pi/`.
- `host/serial_link.py` removed (or shimmed if R4 finds out-of-tree consumers).
- No changes to React app, Teensy firmware, deploy scripts, or docs other than
  this spec and a brief note in `hardware-scripts/README.md` pointing at
  `kart_link/`.
