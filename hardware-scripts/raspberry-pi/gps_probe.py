#!/usr/bin/env python3
"""Probe NEO-M9N GPS visibility from Raspberry Pi over I2C and/or serial."""

from __future__ import annotations

import argparse
import json
import string
import sys
import time
from typing import Any, Dict, List, Tuple

try:
    from smbus2 import SMBus
except ImportError as exc:  # pragma: no cover
    print("ERROR: smbus2 not installed. Run: pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(1) from exc


def _try_import_serial():
    try:
        import serial  # type: ignore

        return serial
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("pyserial is not installed. Run: pip install -r requirements.txt") from exc


def _to_hex(data: bytes) -> str:
    return data.hex().upper()


def probe_i2c(bus_id: int, address: int) -> Dict[str, Any]:
    """
    Probe u-blox DDC/I2C interface.
    For NEO-M9N, bytes available can be read from regs 0xFD/0xFE, stream at 0xFF.
    """
    with SMBus(bus_id) as bus:
        available_hi = bus.read_byte_data(address, 0xFD)
        available_lo = bus.read_byte_data(address, 0xFE)
        available = (available_hi << 8) | available_lo

        sample_len = min(available, 64)
        sample = bytearray()
        for _ in range(sample_len):
            sample.append(bus.read_byte_data(address, 0xFF))

    ascii_preview = "".join(chr(b) if chr(b) in string.printable and b >= 0x20 else "." for b in sample)
    has_ubx_header = len(sample) >= 2 and sample[0] == 0xB5 and sample[1] == 0x62
    has_nmea = "$GP" in ascii_preview or "$GN" in ascii_preview

    return {
        "mode": "i2c",
        "bus": bus_id,
        "address": f"0x{address:02X}",
        "bytes_available": available,
        "sample_hex": _to_hex(bytes(sample)),
        "sample_ascii": ascii_preview,
        "looks_like_gps_stream": bool(has_ubx_header or has_nmea or available > 0),
    }


def probe_serial(device: str, baud: int, timeout_s: float) -> Dict[str, Any]:
    serial = _try_import_serial()
    lines: List[str] = []
    raw_chunks: List[str] = []

    with serial.Serial(device, baudrate=baud, timeout=0.2) as ser:
        ser.reset_input_buffer()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            data = ser.readline()
            if not data:
                continue
            decoded = data.decode(errors="replace").strip()
            if not decoded:
                continue
            raw_chunks.append(decoded)
            if decoded.startswith("$"):
                lines.append(decoded)
            if len(lines) >= 5:
                break

    return {
        "mode": "serial",
        "device": device,
        "baud": baud,
        "nmea_lines": lines,
        "preview": raw_chunks[:10],
        "looks_like_gps_stream": bool(lines),
    }


def run_auto_mode(args: argparse.Namespace) -> Tuple[Dict[str, Any], bool]:
    # Prefer documented wiring path (I2C), then fall back to serial if provided.
    try:
        result = probe_i2c(args.bus, args.i2c_address)
        if result.get("looks_like_gps_stream"):
            return result, True
        if args.serial_device:
            serial_result = probe_serial(args.serial_device, args.baud, args.timeout)
            return {
                "mode": "auto",
                "i2c": result,
                "serial": serial_result,
                "looks_like_gps_stream": bool(serial_result.get("looks_like_gps_stream") or result.get("looks_like_gps_stream")),
            }, bool(serial_result.get("looks_like_gps_stream") or result.get("looks_like_gps_stream"))
        return result, bool(result.get("looks_like_gps_stream"))
    except Exception as i2c_exc:
        if not args.serial_device:
            raise
        serial_result = probe_serial(args.serial_device, args.baud, args.timeout)
        return {
            "mode": "auto",
            "i2c_error": str(i2c_exc),
            "serial": serial_result,
            "looks_like_gps_stream": bool(serial_result.get("looks_like_gps_stream")),
        }, bool(serial_result.get("looks_like_gps_stream"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe NEO-M9N GPS on Raspberry Pi")
    parser.add_argument("--mode", choices=["auto", "i2c", "serial"], default="auto")
    parser.add_argument("--bus", type=int, default=1, help="I2C bus number (default: 1)")
    parser.add_argument("--i2c-address", type=lambda x: int(x, 0), default=0x42, help="GPS I2C address (default: 0x42)")
    parser.add_argument("--serial-device", default="", help="Serial device for NMEA stream (optional in auto mode)")
    parser.add_argument("--baud", type=int, default=9600, help="GPS serial baud (default: 9600)")
    parser.add_argument("--timeout", type=float, default=3.0, help="Serial probe timeout seconds (default: 3.0)")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON")
    args = parser.parse_args()

    try:
        if args.mode == "i2c":
            result = probe_i2c(args.bus, args.i2c_address)
            ok = bool(result.get("looks_like_gps_stream"))
        elif args.mode == "serial":
            if not args.serial_device:
                print("ERROR: --serial-device is required for --mode serial", file=sys.stderr)
                return 1
            result = probe_serial(args.serial_device, args.baud, args.timeout)
            ok = bool(result.get("looks_like_gps_stream"))
        else:
            result, ok = run_auto_mode(args)
    except FileNotFoundError as exc:
        print(f"ERROR: device not found: {exc}", file=sys.stderr)
        return 1
    except PermissionError as exc:
        print(f"ERROR: permission denied: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"ERROR: GPS probe failed: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print("GPS probe result:")
        print(json.dumps(result, indent=2))

    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
