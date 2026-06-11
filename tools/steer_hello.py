#!/usr/bin/env python3
"""Round-trip CAN hello with the ESP32 steering controller via the Teensy."""

from __future__ import annotations

import argparse
import re
import sys

from serial_link import KartConnectionError, KartLink, KartProtocolError, KartTimeoutError


ACK_RE = re.compile(r"ack=(\d)")
FW_RE = re.compile(r"fw=(\d+)\.(\d+)")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Serial response timeout; must exceed the Teensy's 500 ms CAN wait")
    parser.add_argument("--count", type=int, default=1, help="Number of hellos to exchange")
    return parser


def main() -> int:
    args = build_parser().parse_args()

    try:
        with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
            successes = 0
            for i in range(args.count):
                result = link.command("STEER_HELLO", timeout=args.timeout)
                print(f"[{i + 1}/{args.count}] {result.response}")
                m = ACK_RE.search(result.response)
                if m and m.group(1) == "1":
                    successes += 1
                    fw = FW_RE.search(result.response)
                    if fw:
                        print(f"        ESP32 firmware {fw.group(1)}.{fw.group(2)}")
            print(f"summary: {successes}/{args.count} acks")
            return 0 if successes == args.count else 2
    except (KartConnectionError, KartProtocolError, KartTimeoutError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
