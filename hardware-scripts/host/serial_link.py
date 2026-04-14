#!/usr/bin/env python3
"""Shared serial transport helpers for kart host utilities."""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import List, Optional


class KartProtocolError(RuntimeError):
    """Firmware returned ERR ..."""


class KartTimeoutError(TimeoutError):
    """No terminal firmware response observed before timeout."""


class KartConnectionError(ConnectionError):
    """Serial port could not be opened."""


def _import_serial():
    try:
        import serial  # type: ignore

        return serial
    except ImportError as exc:  # pragma: no cover
        raise KartConnectionError("pyserial is required. Install with: pip install pyserial") from exc


@dataclass
class CommandResult:
    command: str
    response: str
    trace: List[str]


class KartLink:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.5):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._serial = None

    def open(self) -> None:
        serial = _import_serial()
        try:
            self._serial = serial.Serial(self.port, baudrate=self.baud, timeout=0.1)
        except Exception as exc:
            raise KartConnectionError(f"Unable to open serial port {self.port}: {exc}") from exc

    def close(self) -> None:
        if self._serial is not None:
            self._serial.close()
            self._serial = None

    def __enter__(self) -> "KartLink":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @property
    def is_open(self) -> bool:
        return self._serial is not None

    def command(self, command: str, timeout: Optional[float] = None) -> CommandResult:
        if self._serial is None:
            raise KartConnectionError("Serial port is not open")

        cmd = command.strip()
        if not cmd:
            raise ValueError("Command cannot be empty")

        self._serial.reset_input_buffer()
        self._serial.write((cmd + "\n").encode("utf-8"))
        self._serial.flush()

        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
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
        raise KartTimeoutError(f"Timeout waiting for response to '{cmd}'. Recent: {recent}")

    def read_available(self, duration_s: float = 0.5) -> List[str]:
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
