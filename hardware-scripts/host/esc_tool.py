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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="ESC serial utility through kart_controller firmware")
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--arm-seconds", type=float, default=2.0)

    sub = parser.add_subparsers(dest="command", required=True)

    cmd_read = sub.add_parser("read", help="Read bytes currently available from ESC UART")
    cmd_read.add_argument("--max", type=int, default=64)

    cmd_write = sub.add_parser("write", help="Write raw bytes to ESC UART (requires ARM)")
    cmd_write.add_argument("--hex", required=True, help="Hex payload, e.g. A55A0102")

    cmd_watch = sub.add_parser("watch", help="Poll ESC_READ repeatedly")
    cmd_watch.add_argument("--max", type=int, default=64)
    cmd_watch.add_argument("--interval", type=float, default=0.2)
    cmd_watch.add_argument("--duration", type=float, default=5.0)

    return parser


def main() -> int:
    args = build_parser().parse_args()

    try:
        if args.command == "write":
            payload = normalize_hex_bytes(args.hex)
            if args.dry_run:
                print(f"[dry-run] ARM {args.arm_seconds:.2f}")
                print(f"[dry-run] ESC_WRITE {payload}")
                return 0

            with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
                print(link.command(f"ARM {args.arm_seconds:.2f}").response)
                print(link.command(f"ESC_WRITE {payload}").response)
            return 0

        if args.command == "read":
            max_bytes = max(1, min(256, args.max))
            if args.dry_run:
                print(f"[dry-run] ESC_READ {max_bytes}")
                return 0

            with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
                print(link.command(f"ESC_READ {max_bytes}").response)
            return 0

        if args.command == "watch":
            max_bytes = max(1, min(256, args.max))
            if args.dry_run:
                print(f"[dry-run] repeat ESC_READ {max_bytes} every {args.interval:.2f}s for {args.duration:.2f}s")
                return 0

            with KartLink(args.port, baud=args.baud, timeout=max(args.timeout, 2.0)) as link:
                deadline = time.monotonic() + max(0.0, args.duration)
                while time.monotonic() < deadline:
                    result = link.command(f"ESC_READ {max_bytes}")
                    if result.response.strip() != "OK ESC_READ":
                        print(result.response)
                    time.sleep(max(0.02, args.interval))
            return 0

        raise ValueError(f"Unknown command: {args.command}")

    except (KartConnectionError, KartProtocolError, KartTimeoutError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
