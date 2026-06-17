#!/usr/bin/env python3
"""Verify the Teensy <-> Steervo CAN link is passing frames both directions.

Reads the two live consoles and checks the link-health counters that kart-core
and steervo expose, with no motor involvement (the Steervo's kEnableMotorOutput
gate and the Teensy STEER ON gate are irrelevant here — this only watches the
CAN traffic).

  Teensy (/dev/ttyACM0): the `STEER` reply carries rx= / txok= / txfail=.
      rx climbing      -> STEER_STATUS is arriving (Steervo -> Teensy OK)
      txok climbing,   -> our frames are queued AND ACKed by the Steervo
      txfail flat at 0     (bus + Steervo -> Teensy ACK path OK)
  Steervo (/dev/ttyUSB0): the 1 Hz `STAT` line carries set_rx= / tx_fail=.
      set_rx climbing  -> STEER_SET is arriving (Teensy -> Steervo OK)

Usage:
  uv run --with pyserial tools/can_link_check.py
  uv run --with pyserial tools/can_link_check.py --teensy /dev/ttyACM0 \
      --steervo /dev/ttyUSB0 --seconds 3
"""

from __future__ import annotations

import argparse
import re
import sys
import time

import serial  # pyserial


def read_teensy_steer(port: str, baud: int, timeout: float = 1.5) -> dict:
    """Send STEER, return the parsed key=val fields of the reply."""
    with serial.Serial(port, baud, timeout=timeout) as s:
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(b"STEER\n")
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = s.readline().decode("ascii", "replace").strip()
            if line.startswith("OK STEER"):
                fields = {}
                for tok in line.split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        fields[k] = v
                return fields
    raise TimeoutError(f"no 'OK STEER' reply on {port}")


def read_steervo_stat(port: str, baud: int, seconds: float) -> list[dict]:
    """Collect STAT lines from the Steervo for `seconds`."""
    out = []
    with serial.Serial(port, baud, timeout=0.5) as s:
        deadline = time.time() + seconds
        while time.time() < deadline:
            line = s.readline().decode("ascii", "replace").strip()
            if line.startswith("STAT"):
                fields = {}
                for tok in line.split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        fields[k] = v
                out.append(fields)
    return out


def as_int(d: dict, key: str) -> int | None:
    try:
        return int(d[key])
    except (KeyError, ValueError):
        return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--teensy", default="/dev/ttyACM0")
    ap.add_argument("--steervo", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=3.0,
                    help="sampling window for the rate checks")
    args = ap.parse_args()

    ok = True

    # ---- Teensy -> sample STEER twice across the window ----
    print(f"[teensy ] {args.teensy} @ {args.baud}")
    try:
        a = read_teensy_steer(args.teensy, args.baud)
        time.sleep(args.seconds)
        b = read_teensy_steer(args.teensy, args.baud)
    except (TimeoutError, serial.SerialException) as e:
        print(f"  FAIL: {e}")
        return 2

    print(f"  STEER: link={b.get('link')} sv_state={b.get('sv_state')} "
          f"cal={b.get('cal')} fault_bits={b.get('fault_bits')}")
    rx0, rx1 = as_int(a, "rx"), as_int(b, "rx")
    txok0, txok1 = as_int(a, "txok"), as_int(b, "txok")
    txf0, txf1 = as_int(a, "txfail"), as_int(b, "txfail")
    if None in (rx0, rx1, txok0, txok1, txf0, txf1):
        print("  FAIL: STEER reply missing rx/txok/txfail — is the new firmware flashed?")
        return 2

    d_rx, d_txok, d_txf = rx1 - rx0, txok1 - txok0, txf1 - txf0
    print(f"  over {args.seconds:.0f}s: rx +{d_rx}  txok +{d_txok}  txfail +{d_txf}")

    if d_rx <= 0:
        print("  FAIL: no STEER_STATUS received (Steervo -> Teensy DOWN)")
        ok = False
    else:
        print("  PASS: Steervo -> Teensy (STEER_STATUS arriving)")
    if d_txok <= 0:
        print("  FAIL: no frames leaving the Teensy TX mailbox")
        ok = False
    elif d_txf > 0:
        print(f"  FAIL: {d_txf} TX failures — frames not ACKed (bus-off / bitrate mismatch?)")
        ok = False
    else:
        print("  PASS: Teensy TX healthy (frames queued and ACKed)")

    # ---- Steervo -> watch set_rx climb ----
    print(f"[steervo] {args.steervo} @ {args.baud}")
    try:
        stats = read_steervo_stat(args.steervo, args.baud, args.seconds)
    except serial.SerialException as e:
        print(f"  WARN: {e} (skipping Steervo-side check)")
        stats = []

    if len(stats) >= 2:
        s0, s1 = as_int(stats[0], "set_rx"), as_int(stats[-1], "set_rx")
        txf = as_int(stats[-1], "tx_fail")
        if s0 is not None and s1 is not None:
            print(f"  STAT samples={len(stats)}  set_rx +{s1 - s0}  tx_fail={txf}")
            if s1 - s0 > 0:
                print("  PASS: Teensy -> Steervo (STEER_SET arriving)")
            else:
                print("  FAIL: STEER_SET not arriving at the Steervo")
                ok = False
    else:
        print("  WARN: <2 STAT lines seen — check the Steervo port / that it's running")

    print()
    print("RESULT:", "LINK OK (bidirectional)" if ok else "LINK PROBLEM — see above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
