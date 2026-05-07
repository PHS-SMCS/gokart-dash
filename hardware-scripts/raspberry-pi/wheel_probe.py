#!/usr/bin/env python3
"""Probe a USB joystick/wheel via the Linux joystick API.

Reads /dev/input/jsX and prints every axis/button event so controls can be
mapped empirically. No third-party deps.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link.joystick import JS_EVENT_SIZE, parse_event  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Map joystick/wheel inputs")
    parser.add_argument("--device", default="/dev/input/js0")
    parser.add_argument("--quiet-axis", type=int, default=2000,
                        help="Suppress axis deltas smaller than this (noise filter)")
    args = parser.parse_args()

    sys.stdout.reconfigure(line_buffering=True)

    print(f"Opening {args.device} — press Ctrl+C to stop.")
    try:
        f = open(args.device, "rb")
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    axes_last = {}
    axes_range = defaultdict(lambda: [None, None])  # min, max
    buttons_seen = set()
    axes_seen = set()
    init_done = False
    t0 = time.monotonic()

    try:
        while True:
            chunk = f.read(JS_EVENT_SIZE)
            if len(chunk) != JS_EVENT_SIZE:
                continue
            event = parse_event(chunk)
            if event is None:
                continue
            value = event.value
            number = event.number
            tag = "INIT " if event.is_init else "     "

            if event.is_button:
                if event.is_init:
                    buttons_seen.add(number)
                print(f"{tag}BTN  #{number:2d}  value={value}")
            elif event.is_axis:
                if event.is_init:
                    axes_seen.add(number)
                    axes_last[number] = value
                    axes_range[number] = [value, value]
                else:
                    prev = axes_last.get(number, 0)
                    if abs(value - prev) < args.quiet_axis:
                        continue
                    axes_last[number] = value
                    lo, hi = axes_range[number]
                    axes_range[number] = [min(lo, value) if lo is not None else value,
                                          max(hi, value) if hi is not None else value]
                    print(f"{tag}AXIS #{number:2d}  value={value:6d}  range=[{axes_range[number][0]},{axes_range[number][1]}]")
            else:
                print(f"{tag}??   number={number} value={value}")

            if not init_done and time.monotonic() - t0 > 0.5:
                init_done = True
                print(f"--- enumerated {len(axes_seen)} axes, {len(buttons_seen)} buttons ---")
                print(f"--- axes: {sorted(axes_seen)} ---")
                print(f"--- buttons: {sorted(buttons_seen)} ---")
    except KeyboardInterrupt:
        print("\n--- summary ---")
        print(f"axes seen:    {sorted(axes_seen)}")
        print(f"buttons seen: {sorted(buttons_seen)}")
        for ax in sorted(axes_range):
            lo, hi = axes_range[ax]
            print(f"  axis {ax}: min={lo} max={hi} rest={axes_last.get(ax)}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
