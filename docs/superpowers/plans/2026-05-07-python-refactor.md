# Python `hardware-scripts/` Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consolidate three duplicated implementations of the kart line protocol into a single `hardware-scripts/kart_link/` package, extract repeated helpers (`normalize_hex_bytes`, joystick event parsing), and convert the bridge HTTP dispatcher to a route table — without changing externally observable behavior.

**Architecture:** New shared sibling package `hardware-scripts/kart_link/` (modules: `transport`, `protocol`, `errors`, `hex`, `joystick`). Each consumer adds a 3-line `sys.path` stanza to import from it (no install step required, preserves systemd unit behavior). `KartLink` exposes both `send_line` (raw, used by bridge/probes) and `command` (raises on ERR, used by CLIs); both are protected by an always-on `threading.Lock`. The bridge `Handler` swaps `if/elif` chains for a `{(method, path): callable}` route table.

**Tech Stack:** Python 3.10+ (uses `X | None` union syntax in spec), `pyserial`, `smbus2`, stdlib `http.server`, `threading`, `dataclasses`, `pathlib`. No new runtime dependencies.

**Reference:** Spec at `docs/superpowers/specs/2026-05-07-python-refactor-design.md`.

**Verification approach:** No test infrastructure exists in the repo (out of scope per spec). Each consumer migration captures a baseline of `--dry-run` outputs *before* any change, then re-captures after; an empty `diff` is the merge gate. `python -m py_compile` is the static gate.

---

## File Structure

**Create:**
- `hardware-scripts/kart_link/__init__.py` — re-exports the public surface.
- `hardware-scripts/kart_link/errors.py` — `KartConnectionError`, `KartProtocolError`, `KartTimeoutError`.
- `hardware-scripts/kart_link/transport.py` — `KartLink` class, `CommandResult` dataclass.
- `hardware-scripts/kart_link/protocol.py` — `parse_status` pure function.
- `hardware-scripts/kart_link/hex.py` — `normalize_hex_bytes`.
- `hardware-scripts/kart_link/joystick.py` — JS event constants + `parse_event`.
- `docs/superpowers/specs/python-refactor-baselines/` — captured pre-refactor `--dry-run` outputs (deleted after final verification).
- `scripts/refactor_baseline.sh` — script that captures the baselines (kept in repo for future refactors).

**Modify:**
- `hardware-scripts/host/kartctl.py` — drop local `normalize_hex_bytes`; import from `kart_link`.
- `hardware-scripts/host/esc_tool.py` — same.
- `hardware-scripts/host/can_tool.py` — same.
- `hardware-scripts/raspberry-pi/teensy_bridge.py` — replace local `TeensyLink` with `KartLink`; convert dispatcher to route table.
- `hardware-scripts/raspberry-pi/teensy_uart_probe.py` — use `KartLink.send_line`.
- `hardware-scripts/raspberry-pi/wheel_bridge.py` — import joystick constants from `kart_link.joystick`.
- `hardware-scripts/raspberry-pi/wheel_probe.py` — import joystick constants from `kart_link.joystick`.
- `hardware-scripts/README.md` — add a `kart_link/` row to the layout table.

**Delete:**
- `hardware-scripts/host/serial_link.py` — superseded by `kart_link.transport` (after Task 8 confirms no out-of-tree consumers).

---

## Task 1: Capture pre-refactor CLI dry-run baselines

The merge gate for the entire refactor is "every CLI dry-run output is byte-identical to today's." This task captures today's outputs as the baseline.

**Files:**
- Create: `scripts/refactor_baseline.sh`
- Create: `docs/superpowers/specs/python-refactor-baselines/*.txt` (one per CLI)

- [ ] **Step 1: Create the baseline script**

Create `scripts/refactor_baseline.sh` with this exact content:

```bash
#!/usr/bin/env bash
# Capture --dry-run outputs from every hardware-scripts CLI subcommand.
# Run once before refactor (--label=before) and once after (--label=after);
# the two output trees should diff to empty.
set -euo pipefail

LABEL="${1:-before}"
OUT_ROOT="docs/superpowers/specs/python-refactor-baselines/${LABEL}"
mkdir -p "$OUT_ROOT"

run() {
  local name="$1"; shift
  local outfile="$OUT_ROOT/${name}.txt"
  # Combined stdout+stderr; record exit code on its own line.
  set +e
  out="$("$@" 2>&1)"
  code=$?
  set -e
  printf '%s\n--- exit: %d\n' "$out" "$code" > "$outfile"
}

cd hardware-scripts/host

# kartctl: every subcommand with --dry-run (representative args)
run kartctl-ping        python3 kartctl.py --dry-run ping
run kartctl-status      python3 kartctl.py --dry-run status
run kartctl-help-cmd    python3 kartctl.py --dry-run help-cmd
run kartctl-safe        python3 kartctl.py --dry-run safe
run kartctl-disarm      python3 kartctl.py --dry-run disarm
run kartctl-hall        python3 kartctl.py --dry-run hall
run kartctl-output-on   python3 kartctl.py --dry-run output --name brake --state on
run kartctl-output-off  python3 kartctl.py --dry-run output --name brake --state off
run kartctl-speed-low   python3 kartctl.py --dry-run speed --mode low
run kartctl-speed-med   python3 kartctl.py --dry-run speed --mode medium
run kartctl-speed-high  python3 kartctl.py --dry-run speed --mode high
run kartctl-reverse-on  python3 kartctl.py --dry-run reverse --state on
run kartctl-reverse-off python3 kartctl.py --dry-run reverse --state off
run kartctl-brake-on    python3 kartctl.py --dry-run brake --state on
run kartctl-brake-off   python3 kartctl.py --dry-run brake --state off
run kartctl-contactor-on  python3 kartctl.py --dry-run contactor --state on
run kartctl-contactor-off python3 kartctl.py --dry-run contactor --state off
run kartctl-throttle-zero python3 kartctl.py --dry-run throttle --percent 0
run kartctl-throttle-five python3 kartctl.py --dry-run throttle --percent 5
run kartctl-led         python3 kartctl.py --dry-run led --r 100 --g 200 --b 50
run kartctl-esc-write   python3 kartctl.py --dry-run esc-write --hex A55A0102
run kartctl-esc-read    python3 kartctl.py --dry-run esc-read --max 32
run kartctl-can-tx      python3 kartctl.py --dry-run can-tx --id 0x123 --data DEADBEEF
run kartctl-can-poll    python3 kartctl.py --dry-run can-poll --max 4
run kartctl-validate    python3 kartctl.py --dry-run validate bringup --profile bench

# esc_tool subcommands
run esc-read   python3 esc_tool.py --dry-run read --max 32
run esc-write  python3 esc_tool.py --dry-run write --hex A55A0102
run esc-watch  python3 esc_tool.py --dry-run watch --max 32 --interval 0.2 --duration 1.0

# can_tool subcommands
run can-tx     python3 can_tool.py --dry-run tx --id 0x123 --data DEADBEEF
run can-poll   python3 can_tool.py --dry-run poll --max 4

echo "captured ${LABEL} baselines under ${OUT_ROOT}"
```

- [ ] **Step 2: Make it executable and run for the "before" baseline**

```bash
chmod +x scripts/refactor_baseline.sh
./scripts/refactor_baseline.sh before
```

Expected output (last line): `captured before baselines under docs/superpowers/specs/python-refactor-baselines/before`

- [ ] **Step 3: Spot-check one capture file**

```bash
cat docs/superpowers/specs/python-refactor-baselines/before/kartctl-ping.txt
```

Expected output:
```
[dry-run] PING
--- exit: 0
```

- [ ] **Step 4: Commit**

```bash
git add scripts/refactor_baseline.sh docs/superpowers/specs/python-refactor-baselines/before/
git commit -m "test(hw-scripts): capture pre-refactor CLI dry-run baselines

These outputs are the merge gate for the python refactor: post-refactor
captures must diff empty. Script kept for future refactors.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Create `kart_link/errors.py`

Single home for the protocol error types. Identical bodies to today's `host/serial_link.py` exceptions.

**Files:**
- Create: `hardware-scripts/kart_link/errors.py`

- [ ] **Step 1: Create the errors module**

Create `hardware-scripts/kart_link/errors.py` with this exact content:

```python
"""Exception types raised by the kart link transport."""

from __future__ import annotations


class KartProtocolError(RuntimeError):
    """Firmware returned an `ERR ...` response."""


class KartTimeoutError(TimeoutError):
    """No terminal `OK`/`ERR` line observed before timeout."""


class KartConnectionError(ConnectionError):
    """Serial port could not be opened or has disconnected."""
```

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/kart_link/errors.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Commit**

```bash
git add hardware-scripts/kart_link/errors.py
git commit -m "feat(hw-scripts): add kart_link.errors module

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Create `kart_link/hex.py`

Single home for `normalize_hex_bytes`. The current copies differ slightly: `can_tool.py` enforces an 8-byte CAN payload cap. The shared version takes an optional `max_bytes` parameter so all three callers reuse one function.

**Files:**
- Create: `hardware-scripts/kart_link/hex.py`

- [ ] **Step 1: Create the hex module**

Create `hardware-scripts/kart_link/hex.py` with this exact content:

```python
"""Hex payload normalization shared by host and Pi tooling."""

from __future__ import annotations

import string


def normalize_hex_bytes(value: str, *, max_bytes: int | None = None) -> str:
    """Strip non-hex characters, validate even-length, return uppercased hex.

    Args:
        value: Raw user input (may contain whitespace, separators, etc.).
        max_bytes: Optional cap on payload length, in bytes. CAN frames pass
            ``max_bytes=8``; ESC writes pass ``None`` (no cap).

    Raises:
        ValueError: If the cleaned payload is empty, has odd length, or
            exceeds ``max_bytes``.
    """
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned:
        raise ValueError("hex payload is empty")
    if len(cleaned) % 2 != 0:
        raise ValueError("hex payload must contain an even number of nybbles")
    if max_bytes is not None and len(cleaned) > max_bytes * 2:
        raise ValueError(f"hex payload max is {max_bytes} bytes")
    return cleaned.upper()
```

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/kart_link/hex.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Quick sanity import**

```bash
python3 -c "import sys; sys.path.insert(0, 'hardware-scripts'); from kart_link.hex import normalize_hex_bytes; print(normalize_hex_bytes('a55a 01 02')); print(normalize_hex_bytes('DEADBEEF', max_bytes=8))"
```

Expected output:
```
A55A0102
DEADBEEF
```

- [ ] **Step 4: Commit**

```bash
git add hardware-scripts/kart_link/hex.py
git commit -m "feat(hw-scripts): add kart_link.hex module

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Create `kart_link/joystick.py`

Single home for the Linux `js_event` struct format and parsing. Used by both `wheel_bridge.py` and `wheel_probe.py`.

**Files:**
- Create: `hardware-scripts/kart_link/joystick.py`

- [ ] **Step 1: Create the joystick module**

Create `hardware-scripts/kart_link/joystick.py` with this exact content:

```python
"""Linux joystick (js_event) parsing helpers.

Event format per ``include/uapi/linux/joystick.h``:
    __u32 time   (ms timestamp since open)
    __s16 value
    __u8  type   (0x01 button, 0x02 axis; 0x80 bit set on init events)
    __u8  number
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

JS_EVENT_FMT = "IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FMT)

JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02
JS_EVENT_INIT = 0x80


@dataclass(frozen=True)
class JsEvent:
    time_ms: int
    value: int
    is_button: bool
    is_axis: bool
    is_init: bool
    number: int


def parse_event(chunk: bytes) -> JsEvent | None:
    """Parse one js_event from a fixed-size byte chunk. Returns None on short read."""
    if len(chunk) != JS_EVENT_SIZE:
        return None
    time_ms, value, ev_type, number = struct.unpack(JS_EVENT_FMT, chunk)
    is_init = bool(ev_type & JS_EVENT_INIT)
    base = ev_type & ~JS_EVENT_INIT
    return JsEvent(
        time_ms=time_ms,
        value=value,
        is_button=base == JS_EVENT_BUTTON,
        is_axis=base == JS_EVENT_AXIS,
        is_init=is_init,
        number=number,
    )
```

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/kart_link/joystick.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Commit**

```bash
git add hardware-scripts/kart_link/joystick.py
git commit -m "feat(hw-scripts): add kart_link.joystick module

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Create `kart_link/protocol.py`

Pure parsing helpers that don't belong on the transport class. Currently `parse_status` lives in `teensy_bridge.py`.

**Files:**
- Create: `hardware-scripts/kart_link/protocol.py`

- [ ] **Step 1: Create the protocol module**

Create `hardware-scripts/kart_link/protocol.py` with this exact content:

```python
"""Pure parsers for kart firmware response lines.

Transport (write/read/timeout) lives in ``kart_link.transport``; this module
only contains stateless functions that turn firmware text into structured
data.
"""

from __future__ import annotations


def parse_status(line: str) -> dict:
    """Turn ``OK STATUS k=v k=v ...`` into a dict.

    Falls back to ``{"raw": line}`` if the prefix doesn't match. Comma-separated
    integer values become lists; other numeric values become ``int`` or
    ``float`` where parseable; everything else stays as ``str``.
    """
    if not line.startswith("OK STATUS"):
        return {"raw": line}
    out: dict[str, str | float | int | list[int]] = {}
    for tok in line.split()[2:]:
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        if "," in v:
            try:
                out[k] = [int(x) for x in v.split(",")]
                continue
            except ValueError:
                pass
        try:
            out[k] = int(v)
            continue
        except ValueError:
            pass
        try:
            out[k] = float(v)
            continue
        except ValueError:
            pass
        out[k] = v
    return out
```

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/kart_link/protocol.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Commit**

```bash
git add hardware-scripts/kart_link/protocol.py
git commit -m "feat(hw-scripts): add kart_link.protocol module with parse_status

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Create `kart_link/transport.py`

The unified `KartLink` class. Always thread-safe. Exposes `send_line` (raw — used by the bridge and probes) and `command` (raises `KartProtocolError` on `ERR` — used by CLIs). Auto-reopens the serial port after a failure.

**Files:**
- Create: `hardware-scripts/kart_link/transport.py`

- [ ] **Step 1: Create the transport module**

Create `hardware-scripts/kart_link/transport.py` with this exact content:

```python
"""Thread-safe Serial transport for the kart line protocol."""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import List, Optional

from .errors import KartConnectionError, KartProtocolError, KartTimeoutError


def _import_serial():
    try:
        import serial  # type: ignore

        return serial
    except ImportError as exc:  # pragma: no cover
        raise KartConnectionError(
            "pyserial is required. Install with: pip install pyserial"
        ) from exc


@dataclass
class CommandResult:
    command: str
    response: str
    trace: List[str]


class KartLink:
    """Thread-safe single-connection wrapper with auto-reopen.

    Two API tiers:

    * ``send_line`` — write one line, return the first ``OK``/``ERR`` response
      line as-is. Used by the HTTP bridge (which surfaces ERR as data) and by
      diagnostic probes.
    * ``command`` — wraps ``send_line``, raises ``KartProtocolError`` on
      ``ERR`` and returns a ``CommandResult`` on ``OK``. Used by host CLIs.

    A ``threading.Lock`` is held around every transport operation so concurrent
    callers (e.g. the bridge serving multiple HTTP requests) cannot interleave
    writes.
    """

    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.5):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._lock = threading.Lock()
        self._serial = None

    # ------------------------------------------------------------------ open/close

    def open(self) -> None:
        serial = _import_serial()
        try:
            self._serial = serial.Serial(self.port, baudrate=self.baud, timeout=0.1)
        except Exception as exc:
            raise KartConnectionError(
                f"Unable to open serial port {self.port}: {exc}"
            ) from exc

    def close(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None

    def __enter__(self) -> "KartLink":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @property
    def is_open(self) -> bool:
        return self._serial is not None and getattr(self._serial, "is_open", True)

    def _ensure_open(self):
        """Lazy/auto-reopen path used by ``send_line``. Bridge use case."""
        serial = _import_serial()
        if self._serial is None or not getattr(self._serial, "is_open", True):
            try:
                self._serial = serial.Serial(self.port, baudrate=self.baud, timeout=0.2)
                time.sleep(0.05)
                self._serial.reset_input_buffer()
            except Exception as exc:
                raise KartConnectionError(
                    f"Unable to open serial port {self.port}: {exc}"
                ) from exc
        return self._serial

    # ------------------------------------------------------------------ low-level send

    def send_line(self, command: str, timeout: Optional[float] = None) -> str:
        """Send one line, return the first OK/ERR response line.

        Auto-reopens the serial port on the next call if the underlying I/O
        fails. Holds the link's lock for the duration of the round trip.
        """
        cmd = command.strip()
        if not cmd:
            raise ValueError("Command cannot be empty")

        deadline_s = timeout if timeout is not None else self.timeout

        with self._lock:
            try:
                ser = self._ensure_open()
                ser.reset_input_buffer()
                ser.write((cmd + "\n").encode("utf-8"))
                ser.flush()

                deadline = time.monotonic() + deadline_s
                last = ""
                while time.monotonic() < deadline:
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    last = line
                    if line.startswith("OK") or line.startswith("ERR"):
                        return line
                raise KartTimeoutError(
                    f"no OK/ERR for {cmd!r}; last={last!r}"
                )
            except (OSError,) as exc:
                # Force a reopen on the next call.
                self._invalidate_serial()
                raise KartConnectionError(f"serial: {exc}") from exc

    def _invalidate_serial(self):
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
        self._serial = None

    # ------------------------------------------------------------------ high-level command

    def command(self, command: str, timeout: Optional[float] = None) -> CommandResult:
        """Send one line, return CommandResult on OK, raise KartProtocolError on ERR.

        Used by host CLIs that treat ERR as a fatal exception.
        """
        cmd = command.strip()
        if not cmd:
            raise ValueError("Command cannot be empty")

        deadline_s = timeout if timeout is not None else self.timeout

        with self._lock:
            if self._serial is None:
                raise KartConnectionError("Serial port is not open")

            self._serial.reset_input_buffer()
            self._serial.write((cmd + "\n").encode("utf-8"))
            self._serial.flush()

            deadline = time.monotonic() + deadline_s
            trace: List[str] = []
            while time.monotonic() < deadline:
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                trace.append(line)
                if line.startswith("OK"):
                    return CommandResult(command=cmd, response=line, trace=trace)
                if line.startswith("ERR"):
                    raise KartProtocolError(f"{cmd} -> {line}")

            recent = "; ".join(trace[-3:]) if trace else "<no lines>"
            raise KartTimeoutError(
                f"Timeout waiting for response to '{cmd}'. Recent: {recent}"
            )

    # ------------------------------------------------------------------ misc

    def read_available(self, duration_s: float = 0.5) -> List[str]:
        """Drain pending serial input for a fixed window. Existing CLI helper."""
        with self._lock:
            if self._serial is None:
                raise KartConnectionError("Serial port is not open")

            deadline = time.monotonic() + max(0.0, duration_s)
            lines: List[str] = []
            while time.monotonic() < deadline:
                raw = self._serial.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace").strip()
                if text:
                    lines.append(text)
            return lines
```

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/kart_link/transport.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Commit**

```bash
git add hardware-scripts/kart_link/transport.py
git commit -m "feat(hw-scripts): add kart_link.transport with two-tier KartLink API

send_line returns the OK/ERR line as-is for the bridge; command raises
KartProtocolError on ERR for host CLIs. Always thread-safe.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Create `kart_link/__init__.py`

Re-export the public surface so consumers write `from kart_link import KartLink, ...` without knowing the submodule layout.

**Files:**
- Create: `hardware-scripts/kart_link/__init__.py`

- [ ] **Step 1: Create the package init**

Create `hardware-scripts/kart_link/__init__.py` with this exact content:

```python
"""Shared kart-protocol transport and helpers for host and Pi tooling.

Consumers add this stanza at the top of their script to import without an
install step::

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from kart_link import KartLink, normalize_hex_bytes  # ...

The path manipulation is cwd-independent so systemd-launched services keep
working regardless of WorkingDirectory=.
"""

from __future__ import annotations

from .errors import KartConnectionError, KartProtocolError, KartTimeoutError
from .hex import normalize_hex_bytes
from .protocol import parse_status
from .transport import CommandResult, KartLink

__all__ = [
    "CommandResult",
    "KartConnectionError",
    "KartLink",
    "KartProtocolError",
    "KartTimeoutError",
    "normalize_hex_bytes",
    "parse_status",
]
```

- [ ] **Step 2: Compile-check + import smoke test**

```bash
python3 -m py_compile hardware-scripts/kart_link/__init__.py
python3 -c "import sys; sys.path.insert(0, 'hardware-scripts'); import kart_link; print(sorted(kart_link.__all__))"
```

Expected output:
```
['CommandResult', 'KartConnectionError', 'KartLink', 'KartProtocolError', 'KartTimeoutError', 'normalize_hex_bytes', 'parse_status']
```

- [ ] **Step 3: Commit**

```bash
git add hardware-scripts/kart_link/__init__.py
git commit -m "feat(hw-scripts): expose kart_link public surface

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Confirm no out-of-tree consumers of `host/serial_link.py`

Before deleting it in Task 12, prove nothing outside `host/` imports it.

**Files:**
- Read-only check.

- [ ] **Step 1: Grep for cross-tree imports**

```bash
git grep -nE 'serial_link|from serial_link' -- ':!hardware-scripts/host/'
```

Expected: no output (exit 1 from `git grep` is fine — it means no matches).

If matches appear in any file *outside* `hardware-scripts/host/`, stop and inform the user — Task 12 will need to keep a re-export shim instead of deleting the file. This case is risk **R4** in the spec.

- [ ] **Step 2: Confirm in-tree consumers (informational)**

```bash
git grep -nE 'serial_link|from serial_link' -- 'hardware-scripts/host/'
```

Expected: lines from `kartctl.py`, `esc_tool.py`, `can_tool.py` only.

(No commit — this is verification only.)

---

## Task 9: Migrate `host/kartctl.py` to `kart_link`

Replace the local `serial_link` import + local `normalize_hex_bytes` with imports from `kart_link`.

**Files:**
- Modify: `hardware-scripts/host/kartctl.py`

- [ ] **Step 1: Replace the import block**

Replace lines 1–17 of `hardware-scripts/host/kartctl.py`:

```python
#!/usr/bin/env python3
"""Main host CLI for Teensy kart controller diagnostics and control."""

from __future__ import annotations

import argparse
import json
import string
import sys
from typing import Dict, Tuple

from serial_link import (
    KartConnectionError,
    KartLink,
    KartProtocolError,
    KartTimeoutError,
)
```

with:

```python
#!/usr/bin/env python3
"""Main host CLI for Teensy kart controller diagnostics and control."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link import (  # noqa: E402  (sys.path setup must precede import)
    KartConnectionError,
    KartLink,
    KartProtocolError,
    KartTimeoutError,
    normalize_hex_bytes,
)
```

Note the `string` import was removed — it was only used by the now-deleted local `normalize_hex_bytes`.

- [ ] **Step 2: Delete the local `normalize_hex_bytes` definition**

Find and delete this function definition (currently lines 23–29):

```python
def normalize_hex_bytes(value: str) -> str:
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned:
        raise ValueError("hex payload is empty")
    if len(cleaned) % 2 != 0:
        raise ValueError("hex payload must contain an even number of nybbles")
    return cleaned.upper()
```

- [ ] **Step 3: Compile-check**

```bash
python3 -m py_compile hardware-scripts/host/kartctl.py
```

Expected: no output, exit 0.

- [ ] **Step 4: Capture post-migration baseline for kartctl**

```bash
./scripts/refactor_baseline.sh after-kartctl-only
```

Expected: completes successfully.

- [ ] **Step 5: Diff against the pre-refactor baseline**

```bash
diff -ur \
  docs/superpowers/specs/python-refactor-baselines/before/kartctl-* \
  docs/superpowers/specs/python-refactor-baselines/after-kartctl-only/ | head -40
```

Expected: empty output (every `kartctl-*.txt` matches byte-for-byte). If output appears, the migration broke parity — stop and investigate.

Note: the diff command will only compare `kartctl-*` files; the after-baseline also contains `esc-*` and `can-*` outputs which aren't relevant here yet.

- [ ] **Step 6: Commit**

```bash
git add hardware-scripts/host/kartctl.py
git commit -m "refactor(kartctl): import KartLink and helpers from kart_link

Drops the local normalize_hex_bytes copy and the host-local serial_link
import in favor of the shared kart_link package. CLI dry-run output
unchanged (verified against captured baselines).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 7: Discard the intermediate baseline**

```bash
rm -rf docs/superpowers/specs/python-refactor-baselines/after-kartctl-only/
```

The final after-baseline will be captured in Task 17.

---

## Task 10: Migrate `host/esc_tool.py` to `kart_link`

**Files:**
- Modify: `hardware-scripts/host/esc_tool.py`

- [ ] **Step 1: Replace the import block**

Replace lines 1–11 of `hardware-scripts/host/esc_tool.py`:

```python
#!/usr/bin/env python3
"""ESC serial passthrough helper via Teensy firmware (Serial1 bridge)."""

from __future__ import annotations

import argparse
import string
import sys
import time

from serial_link import KartConnectionError, KartLink, KartProtocolError, KartTimeoutError
```

with:

```python
#!/usr/bin/env python3
"""ESC serial passthrough helper via Teensy firmware (Serial1 bridge)."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link import (  # noqa: E402
    KartConnectionError,
    KartLink,
    KartProtocolError,
    KartTimeoutError,
    normalize_hex_bytes,
)
```

- [ ] **Step 2: Delete the local `normalize_hex_bytes` definition**

Find and delete (currently lines 14–18):

```python
def normalize_hex_bytes(value: str) -> str:
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned or len(cleaned) % 2 != 0:
        raise ValueError("--hex must be valid even-length hex")
    return cleaned.upper()
```

Note: the local message is `--hex must be valid even-length hex`, while the shared version's messages are `hex payload is empty` / `hex payload must contain an even number of nybbles`. The error text changes but only surfaces on bad user input. The CLI dry-run baseline does not exercise the error path, so parity diffing won't flag it. **Mention this text-change in the commit message** so it's discoverable later.

- [ ] **Step 3: Compile-check**

```bash
python3 -m py_compile hardware-scripts/host/esc_tool.py
```

Expected: no output, exit 0.

- [ ] **Step 4: Capture and diff**

```bash
./scripts/refactor_baseline.sh after-esc-tool-only
diff -ur \
  docs/superpowers/specs/python-refactor-baselines/before/esc-* \
  docs/superpowers/specs/python-refactor-baselines/after-esc-tool-only/ | head -40
```

Expected: empty output for the `esc-*` files.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/host/esc_tool.py
git commit -m "refactor(esc_tool): import KartLink and helpers from kart_link

CLI dry-run output unchanged. Note: error text on invalid --hex changes
from '--hex must be valid even-length hex' to the shared kart_link
messages ('hex payload is empty' / 'hex payload must contain an even
number of nybbles'). Error path is unreachable in dry-run so baseline
diff stays empty.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 6: Discard intermediate baseline**

```bash
rm -rf docs/superpowers/specs/python-refactor-baselines/after-esc-tool-only/
```

---

## Task 11: Migrate `host/can_tool.py` to `kart_link`

The CAN-specific 8-byte cap is preserved by passing `max_bytes=8` to the shared helper.

**Files:**
- Modify: `hardware-scripts/host/can_tool.py`

- [ ] **Step 1: Replace the import block**

Replace lines 1–10 of `hardware-scripts/host/can_tool.py`:

```python
#!/usr/bin/env python3
"""Focused CAN utility via Teensy firmware command bridge."""

from __future__ import annotations

import argparse
import string
import sys

from serial_link import KartConnectionError, KartLink, KartProtocolError, KartTimeoutError
```

with:

```python
#!/usr/bin/env python3
"""Focused CAN utility via Teensy firmware command bridge."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link import (  # noqa: E402
    KartConnectionError,
    KartLink,
    KartProtocolError,
    KartTimeoutError,
    normalize_hex_bytes as _normalize_hex_raw,
)
```

The `as _normalize_hex_raw` alias gives us a name for the shared base function so we can keep a thin local wrapper that pre-binds the 8-byte cap.

- [ ] **Step 2: Replace the local `normalize_hex_bytes` with a 1-line wrapper**

Replace this function (currently lines 13–19):

```python
def normalize_hex_bytes(value: str) -> str:
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned or len(cleaned) % 2 != 0:
        raise ValueError("--data must be valid even-length hex")
    if len(cleaned) > 16:
        raise ValueError("CAN payload max is 8 bytes")
    return cleaned.upper()
```

with:

```python
def normalize_hex_bytes(value: str) -> str:
    return _normalize_hex_raw(value, max_bytes=8)
```

The error texts change here too (same caveat as Task 10). Document in the commit.

- [ ] **Step 3: Compile-check**

```bash
python3 -m py_compile hardware-scripts/host/can_tool.py
```

Expected: no output, exit 0.

- [ ] **Step 4: Capture and diff**

```bash
./scripts/refactor_baseline.sh after-can-tool-only
diff -ur \
  docs/superpowers/specs/python-refactor-baselines/before/can-* \
  docs/superpowers/specs/python-refactor-baselines/after-can-tool-only/ | head -40
```

Expected: empty output for the `can-*` files.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/host/can_tool.py
git commit -m "refactor(can_tool): import KartLink and normalize_hex_bytes from kart_link

Wraps the shared helper with max_bytes=8 to preserve the CAN payload cap.
Error text on invalid --data changes (same caveat as esc_tool migration).
Dry-run baseline diff is empty.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 6: Discard intermediate baseline**

```bash
rm -rf docs/superpowers/specs/python-refactor-baselines/after-can-tool-only/
```

---

## Task 12: Delete `host/serial_link.py`

Task 8 confirmed nothing outside `host/` imports it. Tasks 9–11 removed all uses inside `host/`. Safe to delete.

**Files:**
- Delete: `hardware-scripts/host/serial_link.py`

- [ ] **Step 1: Re-confirm no remaining importers anywhere**

```bash
git grep -nE '(^|[^.])serial_link' -- ':!docs/' ':!scripts/' ':!hardware-scripts/host/serial_link.py'
```

Expected: no output. (The exclusions skip docs that may quote the old name and the file we're about to delete.)

If anything matches, stop and report — the migration missed a consumer.

- [ ] **Step 2: Delete the file**

```bash
git rm hardware-scripts/host/serial_link.py
```

- [ ] **Step 3: Compile-check the host directory**

```bash
python3 -m py_compile hardware-scripts/host/*.py
```

Expected: no output, exit 0.

- [ ] **Step 4: Commit**

```bash
git commit -m "refactor(hw-scripts): delete host/serial_link.py

Superseded by kart_link.transport. All in-tree consumers migrated in the
preceding commits; verified via 'git grep' that no out-of-tree references
remain.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 13: Migrate `raspberry-pi/teensy_uart_probe.py` to `kart_link`

This script is a thin smoke test that PINGs/STATUSes the Teensy. With `KartLink.send_line` it shrinks dramatically.

**Files:**
- Modify: `hardware-scripts/raspberry-pi/teensy_uart_probe.py`

- [ ] **Step 1: Rewrite the file**

Overwrite `hardware-scripts/raspberry-pi/teensy_uart_probe.py` entirely with:

```python
#!/usr/bin/env python3
"""Probe Raspberry Pi UART link to Teensy kart controller firmware."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link import KartLink, KartConnectionError, KartTimeoutError  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Pi UART path to Teensy firmware")
    parser.add_argument("--device", default="/dev/serial0", help="UART device path (default: /dev/serial0)")
    parser.add_argument("--baud", type=int, default=115200, help="UART baudrate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="Response timeout seconds")
    parser.add_argument("--safe", action="store_true", help="Send SAFE command after successful ping")
    args = parser.parse_args()

    link = KartLink(args.device, baud=args.baud, timeout=args.timeout)
    try:
        ping_resp = link.send_line("PING", timeout=args.timeout)
        print(f"PING -> {ping_resp}")
        if not ping_resp.startswith("OK"):
            return 1

        if args.safe:
            safe_resp = link.send_line("SAFE", timeout=args.timeout)
            print(f"SAFE -> {safe_resp}")
            if not safe_resp.startswith("OK"):
                return 1

        status_resp = link.send_line("STATUS", timeout=args.timeout)
        print(f"STATUS -> {status_resp}")
        return 0
    except (KartConnectionError, KartTimeoutError) as exc:
        print(f"ERROR: UART probe failed: {exc}", file=sys.stderr)
        return 1
    finally:
        link.close()


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/raspberry-pi/teensy_uart_probe.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Verify `--help` still works**

```bash
python3 hardware-scripts/raspberry-pi/teensy_uart_probe.py --help
```

Expected: standard argparse usage block listing `--device`, `--baud`, `--timeout`, `--safe`. Exit 0.

- [ ] **Step 4: Commit**

```bash
git add hardware-scripts/raspberry-pi/teensy_uart_probe.py
git commit -m "refactor(teensy_uart_probe): use kart_link.KartLink

Drops the script-local send_command/_try_import_serial duplication; same
CLI surface, same output format on PING/SAFE/STATUS.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: Migrate `raspberry-pi/teensy_bridge.py` and add route table

Two changes in this task: (a) replace the local `TeensyLink` with `KartLink.send_line`; (b) replace the `if/elif` dispatcher with a `{(method, path): handler}` route table. Both ride together because the route table needs the new link.

**Files:**
- Modify: `hardware-scripts/raspberry-pi/teensy_bridge.py`

- [ ] **Step 1: Rewrite the file**

Overwrite `hardware-scripts/raspberry-pi/teensy_bridge.py` entirely with:

```python
#!/usr/bin/env python3
"""HTTP bridge between the dashboard browser and the Teensy kart controller.

Exposes a tiny localhost-only JSON API. Each endpoint serializes a single
command to the Teensy over /dev/serial0 and returns the firmware's reply.

Endpoints:
    GET  /api/health             -> {"ok": true, "serial": "open"|"closed"}
    GET  /api/status             -> parsed STATUS dict
    POST /api/led {r,g,b}        -> sends `LED <r> <g> <b>`
"""

from __future__ import annotations

import json
import logging
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable, Dict, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link import KartLink, parse_status  # noqa: E402

LOG = logging.getLogger("teensy_bridge")

DEVICE = os.environ.get("TEENSY_DEVICE", "/dev/serial0")
BAUD = int(os.environ.get("TEENSY_BAUD", "115200"))
HOST = os.environ.get("BRIDGE_HOST", "127.0.0.1")
PORT = int(os.environ.get("BRIDGE_PORT", "5174"))
RESPONSE_TIMEOUT_S = 1.0


def clamp_byte(v) -> int:
    n = int(v)
    if n < 0:
        return 0
    if n > 255:
        return 255
    return n


# --- Route handlers -----------------------------------------------------------

def _handle_health(handler: "Handler") -> None:
    handler._send_json(200, {"ok": True, "serial": "open" if handler.link.is_open else "closed"})


def _handle_status(handler: "Handler") -> None:
    try:
        line = handler.link.send_line("STATUS", timeout=RESPONSE_TIMEOUT_S)
    except Exception as exc:
        handler._send_json(503, {"ok": False, "error": str(exc)})
        return
    handler._send_json(200, {"ok": True, "status": parse_status(line), "raw": line})


def _handle_led(handler: "Handler") -> None:
    data = handler._read_json()
    try:
        r = clamp_byte(data.get("r", 0))
        g = clamp_byte(data.get("g", 0))
        b = clamp_byte(data.get("b", 0))
    except (TypeError, ValueError):
        handler._send_json(400, {"ok": False, "error": "r,g,b must be integers"})
        return
    try:
        line = handler.link.send_line(f"LED {r} {g} {b}", timeout=RESPONSE_TIMEOUT_S)
    except Exception as exc:
        handler._send_json(503, {"ok": False, "error": str(exc), "rgb": [r, g, b]})
        return
    ok = line.startswith("OK")
    handler._send_json(200 if ok else 502, {"ok": ok, "rgb": [r, g, b], "raw": line})


ROUTES: Dict[Tuple[str, str], Callable[["Handler"], None]] = {
    ("GET",  "/api/health"): _handle_health,
    ("GET",  "/api/status"): _handle_status,
    ("POST", "/api/led"):    _handle_led,
}


# --- HTTP plumbing ------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "GoKartBridge/1"

    # Single shared link; assigned in main().
    link: KartLink

    def log_message(self, fmt, *args):
        LOG.info("%s - %s", self.address_string(), fmt % args)

    def _send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        # Localhost-only service; the dashboard fetches from a sibling port.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", "0") or 0)
        if length <= 0 or length > 4096:
            return {}
        raw = self.rfile.read(length)
        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            return {}
        return data if isinstance(data, dict) else {}

    def _dispatch(self, method: str) -> None:
        handler = ROUTES.get((method, self.path))
        if handler is None:
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        handler(self)

    def do_OPTIONS(self):
        self._send_json(204, {})

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        stream=sys.stderr,
    )

    Handler.link = KartLink(DEVICE, baud=BAUD, timeout=RESPONSE_TIMEOUT_S)
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    LOG.info("listening on http://%s:%d  (serial=%s @ %d)", HOST, PORT, DEVICE, BAUD)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

Behavior preservation notes:

* The old `TeensyLink.send` raised `RuntimeError("serial: ...")` on serial failure; the new `KartLink.send_line` raises `KartConnectionError("serial: ...")` (a subclass of `ConnectionError`). The handlers catch `Exception`, so the JSON output (`{"ok": false, "error": "serial: ..."}`) is identical in shape and similar in message text.
* The new `KartLink.is_open` is a property (not a method as `TeensyLink.is_open()` was). `_handle_health` uses property-access form (`handler.link.is_open`) — no parentheses.
* `_ensure_open` (auto-reopen) is preserved by `KartLink.send_line`'s internal `_ensure_open` call.
* `RESPONSE_TIMEOUT_S` is now passed explicitly via the `timeout=` keyword to make the constraint visible at the call site.

- [ ] **Step 2: Compile-check**

```bash
python3 -m py_compile hardware-scripts/raspberry-pi/teensy_bridge.py
```

Expected: no output, exit 0.

- [ ] **Step 3: Static check — verify route table contents**

```bash
python3 -c "
import sys; sys.path.insert(0, 'hardware-scripts/raspberry-pi')
sys.path.insert(0, 'hardware-scripts')
import importlib.util
spec = importlib.util.spec_from_file_location('teensy_bridge', 'hardware-scripts/raspberry-pi/teensy_bridge.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
print(sorted(m.ROUTES.keys()))
"
```

Expected output:
```
[('GET', '/api/health'), ('GET', '/api/status'), ('POST', '/api/led')]
```

- [ ] **Step 4: Commit**

```bash
git add hardware-scripts/raspberry-pi/teensy_bridge.py
git commit -m "refactor(teensy_bridge): consume kart_link, route-table dispatch

Replaces the local TeensyLink with KartLink.send_line (same thread-safe,
auto-reopen semantics, just shared with host CLIs and probes). Replaces
the if/elif dispatcher in do_GET/do_POST with a ROUTES table; adding
endpoints is now one entry plus a handler.

Live HTTP behavior (status codes, JSON shapes, CORS headers) preserved.
Hardware verification deferred to deploy-test on the Pi.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 15: Migrate `raspberry-pi/wheel_bridge.py` to `kart_link.joystick`

Replace the local JS event constants with imports from `kart_link.joystick`. Keep the local `_try_import_serial` (this script does not use `KartLink` — it writes to the UART directly because the firmware command is single-byte fast-path, not request/response).

**Files:**
- Modify: `hardware-scripts/raspberry-pi/wheel_bridge.py`

- [ ] **Step 1: Replace the import block and constant declarations**

Replace lines 1–25 of `hardware-scripts/raspberry-pi/wheel_bridge.py`:

```python
#!/usr/bin/env python3
"""Forward Hori wheel button events from the Pi to the Teensy.

Reads the Linux joystick API (/dev/input/js0) and emits `WHEEL_BTN <idx> <0|1>`
lines to the Teensy over the Pi↔Teensy UART bridge (/dev/serial0 by default).
For now the Teensy firmware uses these to light its onboard LED.

Handles wheel hot-unplug by retrying with backoff. Axis events are captured
but not yet forwarded (no axis command in firmware yet).
"""

from __future__ import annotations

import argparse
import errno
import struct
import sys
import time

JS_EVENT_FMT = "IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FMT)

JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02
JS_EVENT_INIT = 0x80
```

with:

```python
#!/usr/bin/env python3
"""Forward Hori wheel button events from the Pi to the Teensy.

Reads the Linux joystick API (/dev/input/js0) and emits `WHEEL_BTN <idx> <0|1>`
lines to the Teensy over the Pi↔Teensy UART bridge (/dev/serial0 by default).
For now the Teensy firmware uses these to light its onboard LED.

Handles wheel hot-unplug by retrying with backoff. Axis events are captured
but not yet forwarded (no axis command in firmware yet).
"""

from __future__ import annotations

import argparse
import errno
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link.joystick import JS_EVENT_SIZE, parse_event  # noqa: E402
```

- [ ] **Step 2: Update the event-decode block to use `parse_event`**

Find the loop body that currently looks like:

```python
                    chunk = wheel.read(JS_EVENT_SIZE)
                    if len(chunk) != JS_EVENT_SIZE:
                        break
                    _, value, ev_type, number = struct.unpack(JS_EVENT_FMT, chunk)
                    is_init = bool(ev_type & JS_EVENT_INIT)
                    base = ev_type & ~JS_EVENT_INIT

                    if base == JS_EVENT_BUTTON and not is_init and number <= args.max_button:
                        send_wheel_btn(ser, number, value == 1, verbose)

                    drain_responses(ser, verbose)
```

Replace with:

```python
                    chunk = wheel.read(JS_EVENT_SIZE)
                    if len(chunk) != JS_EVENT_SIZE:
                        break
                    event = parse_event(chunk)
                    if event is None:
                        break

                    if event.is_button and not event.is_init and event.number <= args.max_button:
                        send_wheel_btn(ser, event.number, event.value == 1, verbose)

                    drain_responses(ser, verbose)
```

- [ ] **Step 3: Compile-check**

```bash
python3 -m py_compile hardware-scripts/raspberry-pi/wheel_bridge.py
```

Expected: no output, exit 0.

- [ ] **Step 4: Verify `--help` still works**

```bash
python3 hardware-scripts/raspberry-pi/wheel_bridge.py --help
```

Expected: argparse usage listing `--wheel`, `--serial`, `--baud`, `--max-button`, `--quiet`. Exit 0.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/raspberry-pi/wheel_bridge.py
git commit -m "refactor(wheel_bridge): use kart_link.joystick for js_event parsing

Behavior unchanged. The local js_event struct format and constants are
deduplicated with wheel_probe.py via kart_link.joystick.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 16: Migrate `raspberry-pi/wheel_probe.py` to `kart_link.joystick`

**Files:**
- Modify: `hardware-scripts/raspberry-pi/wheel_probe.py`

- [ ] **Step 1: Replace the import block and constant declarations**

Replace lines 1–27 of `hardware-scripts/raspberry-pi/wheel_probe.py`:

```python
#!/usr/bin/env python3
"""Probe a USB joystick/wheel via the Linux joystick API.

Reads /dev/input/jsX and prints every axis/button event so controls can be
mapped empirically. No third-party deps.

Event format (struct js_event): 8 bytes
  __u32 time  (ms timestamp)
  __s16 value
  __u8  type  (0x01 button, 0x02 axis; 0x80 bit set on init events)
  __u8  number
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from collections import defaultdict

JS_EVENT_FMT = "IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FMT)

JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02
JS_EVENT_INIT = 0x80
```

with:

```python
#!/usr/bin/env python3
"""Probe a USB joystick/wheel via the Linux joystick API.

Reads /dev/input/jsX and prints every axis/button event so controls can be
mapped empirically. No third-party deps.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link.joystick import JS_EVENT_SIZE, parse_event  # noqa: E402
```

- [ ] **Step 2: Update the event-decode block to use `parse_event`**

Find the body of the inner `while True:` loop that currently begins with:

```python
            chunk = f.read(JS_EVENT_SIZE)
            if len(chunk) != JS_EVENT_SIZE:
                continue
            ts_ms, value, ev_type, number = struct.unpack(JS_EVENT_FMT, chunk)
            is_init = bool(ev_type & JS_EVENT_INIT)
            base = ev_type & ~JS_EVENT_INIT
            tag = "INIT " if is_init else "     "

            if base == JS_EVENT_BUTTON:
                if is_init:
                    buttons_seen.add(number)
                print(f"{tag}BTN  #{number:2d}  value={value}")
            elif base == JS_EVENT_AXIS:
                if is_init:
                    axes_seen.add(number)
                    axes_last[number] = value
                    axes_range[number] = [value, value]
                else:
                    prev = axes_last.get(number, 0)
                    if abs(value - prev) < args.quiet_axis:
                        continue
                    axes_last[number] = value
                    lo, hi = axes_range[number]
                    axes_range[number] = [min(lo, value) if lo is not None else value,
                                          max(hi, value) if hi is not None else value]
                    print(f"{tag}AXIS #{number:2d}  value={value:6d}  range=[{axes_range[number][0]},{axes_range[number][1]}]")
            else:
                print(f"{tag}??   type={ev_type:#x} number={number} value={value}")
```

Replace with:

```python
            chunk = f.read(JS_EVENT_SIZE)
            if len(chunk) != JS_EVENT_SIZE:
                continue
            event = parse_event(chunk)
            if event is None:
                continue
            value = event.value
            number = event.number
            tag = "INIT " if event.is_init else "     "

            if event.is_button:
                if event.is_init:
                    buttons_seen.add(number)
                print(f"{tag}BTN  #{number:2d}  value={value}")
            elif event.is_axis:
                if event.is_init:
                    axes_seen.add(number)
                    axes_last[number] = value
                    axes_range[number] = [value, value]
                else:
                    prev = axes_last.get(number, 0)
                    if abs(value - prev) < args.quiet_axis:
                        continue
                    axes_last[number] = value
                    lo, hi = axes_range[number]
                    axes_range[number] = [min(lo, value) if lo is not None else value,
                                          max(hi, value) if hi is not None else value]
                    print(f"{tag}AXIS #{number:2d}  value={value:6d}  range=[{axes_range[number][0]},{axes_range[number][1]}]")
            else:
                print(f"{tag}??   number={number} value={value}")
```

The "??" branch loses the `type=0xN` field since `parse_event` doesn't expose the raw type byte. This branch is unreachable in practice (the kernel only emits BUTTON or AXIS), so dropping the diagnostic field is acceptable — and `event is None` (also defensive) catches malformed structs above. **Note this in the commit.**

- [ ] **Step 3: Compile-check**

```bash
python3 -m py_compile hardware-scripts/raspberry-pi/wheel_probe.py
```

Expected: no output, exit 0.

- [ ] **Step 4: Verify `--help` still works**

```bash
python3 hardware-scripts/raspberry-pi/wheel_probe.py --help
```

Expected: argparse usage listing `--device`, `--quiet-axis`. Exit 0.

- [ ] **Step 5: Commit**

```bash
git add hardware-scripts/raspberry-pi/wheel_probe.py
git commit -m "refactor(wheel_probe): use kart_link.joystick for js_event parsing

Behavior unchanged for the BUTTON/AXIS paths. The unreachable '??' branch
loses the raw type-byte diagnostic since parse_event abstracts it; the
kernel never emits other types so this is fine.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 17: Update `hardware-scripts/README.md`

Add a row pointing at the new shared package and a short note in the directory layout description.

**Files:**
- Read first (file size unknown to this plan): `hardware-scripts/README.md`
- Modify: `hardware-scripts/README.md`

- [ ] **Step 1: Read the current README**

```bash
cat hardware-scripts/README.md
```

(No expected output — this is informational. Identify the section listing
the directory layout.)

- [ ] **Step 2: Insert a `kart_link/` row in the layout table**

Locate the existing layout description (a table or list referencing `host/`,
`raspberry-pi/`, and `teensy-4.1/`). Insert a row immediately after the
heading row in the same format the existing rows use, with content equivalent
to:

```
| `kart_link/` | Shared kart-protocol transport (`KartLink`), helpers (`normalize_hex_bytes`, `parse_status`, joystick events), error types. Imported by both `host/` and `raspberry-pi/` scripts via a 3-line `sys.path` stanza — no install step. |
```

If the layout is a bullet list rather than a table, use the same prose with
the format that fits the existing style.

- [ ] **Step 3: Compile-check is N/A (markdown).**

Skip — but verify it renders by inspection:

```bash
git diff hardware-scripts/README.md
```

Expected: a single new row/line about `kart_link/`.

- [ ] **Step 4: Commit**

```bash
git add hardware-scripts/README.md
git commit -m "docs(hw-scripts): mention kart_link/ shared package

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 18: Final verification — full baseline diff and static gate

This is the merge gate.

**Files:**
- Read-only verification.
- Delete (after success): `docs/superpowers/specs/python-refactor-baselines/`

- [ ] **Step 1: Capture the post-refactor baseline**

```bash
./scripts/refactor_baseline.sh after
```

Expected: completes successfully.

- [ ] **Step 2: Diff before/ vs after/**

```bash
diff -ur \
  docs/superpowers/specs/python-refactor-baselines/before/ \
  docs/superpowers/specs/python-refactor-baselines/after/
```

Expected: empty output.

If non-empty, the refactor broke CLI parity. Investigate, fix, re-run from
this step. Do not proceed.

- [ ] **Step 3: Static gate — compile every changed Python file**

```bash
python3 -m py_compile $(git ls-files 'hardware-scripts/**/*.py')
```

Expected: no output, exit 0.

- [ ] **Step 4: Confirm the `_try_import_serial` boilerplate is gone from migrated scripts**

```bash
git grep -nE '_try_import_serial' -- hardware-scripts/
```

Expected: matches only in the still-direct-pyserial scripts that were
intentionally left alone (`gps_probe.py`, `imu_probe.py`, `i2c_scan.py` —
these use `smbus2`/`serial` for hardware probing without going through the
kart protocol). `wheel_bridge.py` and `teensy_uart_probe.py` should NOT
appear. If they do, they were missed.

- [ ] **Step 5: Delete the baseline capture directory**

The script is kept (it's useful for any future refactor); the captures
themselves served their purpose.

```bash
git rm -r docs/superpowers/specs/python-refactor-baselines/
```

- [ ] **Step 6: Final commit**

```bash
git commit -m "chore(hw-scripts): drop refactor verification baselines

CLI parity verified: pre/post --dry-run captures diffed empty across all
host/* and esc_tool/can_tool subcommands. Hardware-dependent checks
(bridge live JSON, wheel events) are deferred to deploy-test on the Pi.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 7: Print a summary report**

```bash
git log --oneline main..HEAD
```

Expected: one commit per task plus the final cleanup. The list should
trace the full migration narrative for code review.

---

## Done criteria

- All 18 tasks above have their checkboxes ticked.
- `git log main..HEAD --stat` shows additions in `hardware-scripts/kart_link/`, modifications across `host/` and `raspberry-pi/`, deletion of `host/serial_link.py`, no changes to React or Teensy code.
- `python -m py_compile $(git ls-files 'hardware-scripts/**/*.py')` exits 0.
- The before/after `--dry-run` baseline diff was empty.
- The hardware-dependent verifications listed in the spec (bridge live JSON shapes, concurrent `/api/led` smoke, wheel-probe live event lines) are either completed on the Pi or explicitly listed in the PR description as deferred to deploy-test.

When all of the above hold, the Python sub-project is complete. Hand back to the user; the next sub-project is the React/TS dashboard refactor (separate spec + plan to be written).
