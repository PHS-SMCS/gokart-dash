#!/usr/bin/env python3
"""Probe a USB joystick/wheel via the Linux joystick API.

Reads /dev/input/jsX and prints every axis/button event so controls can be
mapped empirically. No third-party deps.

Event format (struct js_event): 8 bytes
  __u32 time  (ms timestamp)
  __s16 value
  __u8  type  (0x01 button, 0x02 axis; 0x80 bit set on init events)
  __u8  number
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from collections import defaultdict

JS_EVENT_FMT = "IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FMT)

JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02
JS_EVENT_INIT = 0x80


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
            ts_ms, value, ev_type, number = struct.unpack(JS_EVENT_FMT, chunk)
            is_init = bool(ev_type & JS_EVENT_INIT)
            base = ev_type & ~JS_EVENT_INIT
            tag = "INIT " if is_init else "     "

            if base == JS_EVENT_BUTTON:
                if is_init:
                    buttons_seen.add(number)
                print(f"{tag}BTN  #{number:2d}  value={value}")
            elif base == JS_EVENT_AXIS:
                if is_init:
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
                print(f"{tag}??   type={ev_type:#x} number={number} value={value}")

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
