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
