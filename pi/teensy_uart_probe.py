#!/usr/bin/env python3
"""Probe Raspberry Pi UART link to Teensy kart controller firmware."""

from __future__ import annotations

import argparse
import sys
import time


def _try_import_serial():
    try:
        import serial  # type: ignore

        return serial
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("pyserial is not installed. Run: pip install -r requirements.txt") from exc


def send_command(ser, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command.strip() + "\n").encode())
    ser.flush()

    deadline = time.monotonic() + timeout_s
    last_line = ""
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").strip()
        if not line:
            continue
        last_line = line
        if line.startswith("OK") or line.startswith("ERR"):
            return line
    raise TimeoutError(f"No response for '{command}' (last line: {last_line!r})")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Pi UART path to Teensy firmware")
    parser.add_argument("--device", default="/dev/serial0", help="UART device path (default: /dev/serial0)")
    parser.add_argument("--baud", type=int, default=115200, help="UART baudrate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="Response timeout seconds")
    parser.add_argument("--safe", action="store_true", help="Send SAFE command after successful ping")
    args = parser.parse_args()

    serial = _try_import_serial()

    try:
        with serial.Serial(args.device, baudrate=args.baud, timeout=0.2) as ser:
            ping_resp = send_command(ser, "PING", args.timeout)
            print(f"PING -> {ping_resp}")
            if not ping_resp.startswith("OK"):
                return 1

            if args.safe:
                safe_resp = send_command(ser, "SAFE", args.timeout)
                print(f"SAFE -> {safe_resp}")
                if not safe_resp.startswith("OK"):
                    return 1

            status_resp = send_command(ser, "STATUS", args.timeout)
            print(f"STATUS -> {status_resp}")
            return 0
    except Exception as exc:
        print(f"ERROR: UART probe failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
