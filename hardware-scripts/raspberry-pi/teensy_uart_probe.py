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
