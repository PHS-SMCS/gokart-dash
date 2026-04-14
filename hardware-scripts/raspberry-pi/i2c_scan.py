#!/usr/bin/env python3
"""Scan Raspberry Pi I2C bus for expected kart mainboard devices."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Dict, List

try:
    from smbus2 import SMBus
except ImportError as exc:  # pragma: no cover
    print("ERROR: smbus2 not installed. Run: pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(1) from exc


KNOWN_DEVICES: Dict[int, str] = {
    0x68: "MPU6050 IMU",
    0x42: "NEO-M9N GPS (DDC/I2C, optional depending on module config)",
}


def parse_addr(text: str) -> int:
    value = int(text, 0)
    if not (0x03 <= value <= 0x77):
        raise argparse.ArgumentTypeError(f"I2C address out of range: {text}")
    return value


def probe_address(bus: SMBus, address: int) -> bool:
    try:
        bus.read_byte(address)
        return True
    except OSError:
        return False


def scan_bus(bus_id: int) -> List[int]:
    found: List[int] = []
    with SMBus(bus_id) as bus:
        for address in range(0x03, 0x78):
            if probe_address(bus, address):
                found.append(address)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description="Scan kart I2C bus and validate expected devices")
    parser.add_argument("--bus", type=int, default=1, help="I2C bus number (default: 1)")
    parser.add_argument(
        "--require",
        action="append",
        type=parse_addr,
        default=[0x68],
        help="Required I2C address (repeatable, default includes 0x68)",
    )
    parser.add_argument("--strict", action="store_true", help="Exit non-zero if required addresses are missing")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON")
    args = parser.parse_args()

    # Deduplicate while preserving order
    required: List[int] = list(dict.fromkeys(args.require))

    try:
        found = scan_bus(args.bus)
    except FileNotFoundError:
        print(f"ERROR: /dev/i2c-{args.bus} not found. Is I2C enabled?", file=sys.stderr)
        return 1
    except PermissionError:
        print("ERROR: Permission denied opening I2C bus (try sudo or add user to i2c group)", file=sys.stderr)
        return 1
    except Exception as exc:  # pragma: no cover
        print(f"ERROR: I2C scan failed: {exc}", file=sys.stderr)
        return 1

    missing = [addr for addr in required if addr not in found]

    result = {
        "bus": args.bus,
        "found": [f"0x{addr:02X}" for addr in found],
        "required": [f"0x{addr:02X}" for addr in required],
        "missing": [f"0x{addr:02X}" for addr in missing],
        "known_devices_found": {
            f"0x{addr:02X}": KNOWN_DEVICES.get(addr, "Unknown") for addr in found
        },
    }

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"I2C bus: /dev/i2c-{args.bus}")
        print("Found addresses:")
        if not found:
            print("  (none)")
        for addr in found:
            label = KNOWN_DEVICES.get(addr, "Unknown")
            print(f"  0x{addr:02X}  {label}")

        if required:
            print("Required:")
            for addr in required:
                state = "OK" if addr in found else "MISSING"
                print(f"  0x{addr:02X}: {state}")

    if args.strict and missing:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
