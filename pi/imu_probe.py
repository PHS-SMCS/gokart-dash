#!/usr/bin/env python3
"""Probe MPU6050 on Raspberry Pi I2C bus and print sample telemetry."""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass
from typing import List

try:
    from smbus2 import SMBus
except ImportError as exc:  # pragma: no cover
    print("ERROR: smbus2 not installed. Run: pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(1) from exc


REG_PWR_MGMT_1 = 0x6B
REG_WHO_AM_I = 0x75
REG_ACCEL_XOUT_H = 0x3B


@dataclass
class ImuSample:
    ax_g: float
    ay_g: float
    az_g: float
    gx_dps: float
    gy_dps: float
    gz_dps: float
    temp_c: float


def twos_complement(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def read_word_signed(bus: SMBus, addr: int, reg: int) -> int:
    high = bus.read_byte_data(addr, reg)
    low = bus.read_byte_data(addr, reg + 1)
    return twos_complement((high << 8) | low)


def read_sample(bus: SMBus, addr: int) -> ImuSample:
    ax_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 0)
    ay_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 2)
    az_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 4)
    temp_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 6)
    gx_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 8)
    gy_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 10)
    gz_raw = read_word_signed(bus, addr, REG_ACCEL_XOUT_H + 12)

    # Default full-scale assumptions:
    # accel ±2g => 16384 LSB/g, gyro ±250 dps => 131 LSB/(deg/s)
    return ImuSample(
        ax_g=ax_raw / 16384.0,
        ay_g=ay_raw / 16384.0,
        az_g=az_raw / 16384.0,
        gx_dps=gx_raw / 131.0,
        gy_dps=gy_raw / 131.0,
        gz_dps=gz_raw / 131.0,
        temp_c=(temp_raw / 340.0) + 36.53,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Read identity + samples from MPU6050")
    parser.add_argument("--bus", type=int, default=1, help="I2C bus number (default: 1)")
    parser.add_argument("--address", type=lambda x: int(x, 0), default=0x68, help="MPU6050 I2C address (default: 0x68)")
    parser.add_argument("--samples", type=int, default=3, help="Number of samples to capture")
    parser.add_argument("--interval", type=float, default=0.05, help="Seconds between samples")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON")
    args = parser.parse_args()

    if args.samples < 1:
        print("ERROR: --samples must be >= 1", file=sys.stderr)
        return 1

    try:
        with SMBus(args.bus) as bus:
            # Wake up sensor (device may be in sleep after power-on)
            bus.write_byte_data(args.address, REG_PWR_MGMT_1, 0x00)
            time.sleep(0.05)

            who_am_i = bus.read_byte_data(args.address, REG_WHO_AM_I)
            samples: List[ImuSample] = []
            for i in range(args.samples):
                samples.append(read_sample(bus, args.address))
                if i < args.samples - 1:
                    time.sleep(args.interval)
    except FileNotFoundError:
        print(f"ERROR: /dev/i2c-{args.bus} not found. Is I2C enabled?", file=sys.stderr)
        return 1
    except PermissionError:
        print("ERROR: Permission denied opening I2C bus", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"ERROR: IMU probe failed: {exc}", file=sys.stderr)
        return 1

    identity_ok = who_am_i in (0x68, 0x69)
    payload = {
        "bus": args.bus,
        "address": f"0x{args.address:02X}",
        "who_am_i": f"0x{who_am_i:02X}",
        "identity_ok": identity_ok,
        "samples": [asdict(sample) for sample in samples],
    }

    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print(f"MPU6050 @ 0x{args.address:02X} on /dev/i2c-{args.bus}")
        print(f"WHO_AM_I: 0x{who_am_i:02X} ({'OK' if identity_ok else 'UNEXPECTED'})")
        for idx, sample in enumerate(samples, start=1):
            print(
                f"sample#{idx}: "
                f"accel[g]=({sample.ax_g:+.3f}, {sample.ay_g:+.3f}, {sample.az_g:+.3f}) "
                f"gyro[dps]=({sample.gx_dps:+.2f}, {sample.gy_dps:+.2f}, {sample.gz_dps:+.2f}) "
                f"temp={sample.temp_c:.2f}C"
            )

    return 0 if identity_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
