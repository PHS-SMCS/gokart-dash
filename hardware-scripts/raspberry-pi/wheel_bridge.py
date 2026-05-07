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


def _try_import_serial():
    try:
        import serial  # type: ignore
        return serial
    except ImportError as exc:
        raise RuntimeError("pyserial is not installed. Run: pip install -r requirements.txt") from exc


def open_wheel(path: str, verbose: bool):
    while True:
        try:
            f = open(path, "rb")
            if verbose:
                print(f"[wheel] opened {path}", flush=True)
            return f
        except OSError as exc:
            if exc.errno in (errno.ENOENT, errno.ENODEV, errno.EACCES):
                if verbose:
                    print(f"[wheel] waiting for {path} ({exc})", flush=True)
                time.sleep(1.0)
                continue
            raise


def send_wheel_btn(ser, idx: int, pressed: bool, verbose: bool) -> None:
    line = f"WHEEL_BTN {idx} {1 if pressed else 0}\n"
    ser.write(line.encode())
    ser.flush()
    if verbose:
        print(f"[tx] {line.strip()}", flush=True)


def drain_responses(ser, verbose: bool) -> None:
    while ser.in_waiting:
        try:
            raw = ser.readline()
        except Exception:
            return
        if not raw:
            return
        text = raw.decode(errors="replace").strip()
        if text and verbose:
            print(f"[rx] {text}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Forward wheel buttons to Teensy")
    parser.add_argument("--wheel", default="/dev/input/js0")
    parser.add_argument("--serial", default="/dev/serial0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--max-button", type=int, default=10,
                        help="Highest button index to forward (firmware accepts 0..10)")
    parser.add_argument("--quiet", action="store_true", help="Only log transmissions")
    args = parser.parse_args()
    verbose = not args.quiet

    serial_mod = _try_import_serial()
    try:
        ser = serial_mod.Serial(args.serial, baudrate=args.baud, timeout=0.2)
    except Exception as exc:
        print(f"ERROR: cannot open {args.serial}: {exc}", file=sys.stderr)
        return 1

    try:
        while True:
            wheel = open_wheel(args.wheel, verbose)
            try:
                while True:
                    chunk = wheel.read(JS_EVENT_SIZE)
                    if len(chunk) != JS_EVENT_SIZE:
                        break
                    event = parse_event(chunk)
                    if event is None:
                        break

                    if event.is_button and not event.is_init and event.number <= args.max_button:
                        send_wheel_btn(ser, event.number, event.value == 1, verbose)

                    drain_responses(ser, verbose)
            except OSError as exc:
                if exc.errno == errno.ENODEV:
                    if verbose:
                        print("[wheel] disconnected, waiting for reconnect", flush=True)
                else:
                    raise
            finally:
                try:
                    wheel.close()
                except Exception:
                    pass
    except KeyboardInterrupt:
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
