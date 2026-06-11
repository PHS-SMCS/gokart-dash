#!/usr/bin/env python3
"""Focused CAN utility via Teensy firmware command bridge."""

from __future__ import annotations

import argparse
import string
import sys

from serial_link import KartConnectionError, KartLink, KartProtocolError, KartTimeoutError


def normalize_hex_bytes(value: str) -> str:
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned or len(cleaned) % 2 != 0:
        raise ValueError("--data must be valid even-length hex")
    if len(cleaned) > 16:
        raise ValueError("CAN payload max is 8 bytes")
    return cleaned.upper()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="CAN helper through kart_controller firmware")
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--arm-seconds", type=float, default=2.0)

    sub = parser.add_subparsers(dest="command", required=True)

    tx = sub.add_parser("tx", help="Transmit one CAN frame")
    tx.add_argument("--id", required=True, help="CAN frame id (decimal or 0x...)")
    tx.add_argument("--data", required=True, help="Hex payload up to 8 bytes")

    poll = sub.add_parser("poll", help="Poll received CAN frames")
    poll.add_argument("--max", type=int, default=8)

    return parser


def main() -> int:
    args = build_parser().parse_args()

    try:
        if args.command == "tx":
            frame_id = int(args.id, 0)
            payload = normalize_hex_bytes(args.data)
            if args.dry_run:
                print(f"[dry-run] ARM {args.arm_seconds:.2f}")
                print(f"[dry-run] CAN_TX {frame_id} {payload}")
                return 0

            with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
                print(link.command(f"ARM {args.arm_seconds:.2f}").response)
                print(link.command(f"CAN_TX {frame_id} {payload}").response)
            return 0

        if args.command == "poll":
            max_frames = max(1, min(64, args.max))
            if args.dry_run:
                print(f"[dry-run] CAN_POLL {max_frames}")
                return 0

            with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
                result = link.command(f"CAN_POLL {max_frames}", timeout=max(args.timeout, 2.5))
                for line in result.trace[:-1]:
                    print(line)
                print(result.response)
            return 0

        raise ValueError(f"Unknown command: {args.command}")

    except (KartConnectionError, KartProtocolError, KartTimeoutError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
