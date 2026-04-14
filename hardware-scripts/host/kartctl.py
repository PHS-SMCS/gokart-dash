#!/usr/bin/env python3
"""Main host CLI for Teensy kart controller diagnostics and control."""

from __future__ import annotations

import argparse
import json
import string
import sys
from typing import Dict, Tuple

from serial_link import (
    KartConnectionError,
    KartLink,
    KartProtocolError,
    KartTimeoutError,
)


HAZARD_HINT = "(hazardous command: requires ARM window unless --dry-run)"


def normalize_hex_bytes(value: str) -> str:
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned:
        raise ValueError("hex payload is empty")
    if len(cleaned) % 2 != 0:
        raise ValueError("hex payload must contain an even number of nybbles")
    return cleaned.upper()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Host control utility for SMCSKart Teensy firmware")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial device to Teensy (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=1.5, help="Response timeout seconds (default: 1.5)")
    parser.add_argument("--dry-run", action="store_true", help="Print commands but do not send to hardware")
    parser.add_argument(
        "--arm-seconds",
        type=float,
        default=2.0,
        help="ARM window requested before hazardous commands (default: 2.0)",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("ping", help="Firmware ping")
    sub.add_parser("status", help="Get firmware status")
    sub.add_parser("help-cmd", help="Get firmware command list")
    sub.add_parser("safe", help="Apply SAFE state")
    sub.add_parser("disarm", help="Disarm and apply safe outputs")
    sub.add_parser("hall", help="Read hall pulse counter")

    p_output = sub.add_parser("output", help=f"Set named output state {HAZARD_HINT}")
    p_output.add_argument("--name", choices=["reverse", "brake", "speed_low", "speed_high", "cruise", "contactor"], required=True)
    p_output.add_argument("--state", choices=["on", "off"], required=True)

    p_speed = sub.add_parser("speed", help=f"Set speed mode {HAZARD_HINT}")
    p_speed.add_argument("--mode", choices=["low", "medium", "high"], required=True)

    p_reverse = sub.add_parser("reverse", help=f"Reverse toggle {HAZARD_HINT}")
    p_reverse.add_argument("--state", choices=["on", "off"], required=True)

    p_brake = sub.add_parser("brake", help=f"Brake toggle {HAZARD_HINT}")
    p_brake.add_argument("--state", choices=["on", "off"], required=True)

    p_contactor = sub.add_parser("contactor", help=f"Contactor toggle {HAZARD_HINT}")
    p_contactor.add_argument("--state", choices=["on", "off"], required=True)

    p_throttle = sub.add_parser("throttle", help=f"Set throttle percentage {HAZARD_HINT}")
    p_throttle.add_argument("--percent", type=float, required=True)

    p_led = sub.add_parser("led", help="Set LED RGB (0-255)")
    p_led.add_argument("--r", type=int, required=True)
    p_led.add_argument("--g", type=int, required=True)
    p_led.add_argument("--b", type=int, required=True)

    p_esc_write = sub.add_parser("esc-write", help=f"Write raw bytes to ESC serial {HAZARD_HINT}")
    p_esc_write.add_argument("--hex", required=True, help="Hex payload, e.g. A55A0102")

    p_esc_read = sub.add_parser("esc-read", help="Read bytes currently available from ESC serial")
    p_esc_read.add_argument("--max", type=int, default=64)

    p_can_tx = sub.add_parser("can-tx", help=f"Transmit CAN frame {HAZARD_HINT}")
    p_can_tx.add_argument("--id", required=True, help="Frame ID (decimal or 0x...)")
    p_can_tx.add_argument("--data", required=True, help="Hex payload up to 8 bytes")

    p_can_poll = sub.add_parser("can-poll", help="Poll received CAN frames")
    p_can_poll.add_argument("--max", type=int, default=8)

    p_validate = sub.add_parser("validate", help="Run guided validation sequences")
    validate_sub = p_validate.add_subparsers(dest="validate_command", required=True)
    p_bringup = validate_sub.add_parser("bringup", help="Bench bring-up validation")
    p_bringup.add_argument("--profile", choices=["bench", "vehicle"], default="bench")

    return parser


def resolve_fw_command(args: argparse.Namespace) -> Tuple[str, bool]:
    if args.command == "ping":
        return "PING", False
    if args.command == "status":
        return "STATUS", False
    if args.command == "help-cmd":
        return "HELP", False
    if args.command == "safe":
        return "SAFE", False
    if args.command == "disarm":
        return "DISARM", False
    if args.command == "hall":
        return "HALL?", False

    if args.command == "output":
        hazardous = args.state == "on"
        return f"OUTPUT {args.name} {args.state}", hazardous

    if args.command == "speed":
        hazardous = args.mode in {"low", "high"}
        return f"SPEED {args.mode}", hazardous

    if args.command == "reverse":
        hazardous = args.state == "on"
        return f"REVERSE {args.state}", hazardous

    if args.command == "brake":
        hazardous = args.state == "on"
        return f"BRAKE {args.state}", hazardous

    if args.command == "contactor":
        hazardous = args.state == "on"
        return f"CONTACTOR {args.state}", hazardous

    if args.command == "throttle":
        hazardous = args.percent > 0.0
        return f"THROTTLE {args.percent:.3f}", hazardous

    if args.command == "led":
        for key in ("r", "g", "b"):
            value = getattr(args, key)
            if value < 0 or value > 255:
                raise ValueError("LED values must be in range 0..255")
        return f"LED {args.r} {args.g} {args.b}", False

    if args.command == "esc-write":
        payload = normalize_hex_bytes(args.hex)
        return f"ESC_WRITE {payload}", True

    if args.command == "esc-read":
        max_bytes = max(1, min(256, args.max))
        return f"ESC_READ {max_bytes}", False

    if args.command == "can-tx":
        payload = normalize_hex_bytes(args.data)
        frame_id = int(args.id, 0)
        if frame_id < 0:
            raise ValueError("CAN id must be >= 0")
        return f"CAN_TX {frame_id} {payload}", True

    if args.command == "can-poll":
        max_frames = max(1, min(64, args.max))
        return f"CAN_POLL {max_frames}", False

    raise ValueError(f"Unsupported command: {args.command}")


def arm_if_needed(link: KartLink, seconds: float) -> None:
    if seconds <= 0:
        raise ValueError("--arm-seconds must be > 0 for hazardous commands")
    arm_resp = link.command(f"ARM {seconds:.2f}")
    print(arm_resp.response)


def run_single(args: argparse.Namespace) -> int:
    fw_command, hazardous = resolve_fw_command(args)

    if args.dry_run:
        if hazardous:
            print(f"[dry-run] ARM {args.arm_seconds:.2f}")
        print(f"[dry-run] {fw_command}")
        return 0

    with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
        if hazardous:
            arm_if_needed(link, args.arm_seconds)
        result = link.command(fw_command)
        print(result.response)
        for line in result.trace[:-1]:
            # Includes asynchronous diagnostic lines preceding terminal OK.
            print(f"trace: {line}")
    return 0


def run_validate_bringup(args: argparse.Namespace) -> int:
    report: Dict[str, object] = {
        "mode": "validate bringup",
        "profile": args.profile,
        "checks": [],
    }

    def add_check(name: str, ok: bool, detail: str) -> None:
        report["checks"].append({"name": name, "ok": ok, "detail": detail})

    if args.dry_run:
        add_check("ping", True, "[dry-run] PING")
        add_check("status", True, "[dry-run] STATUS")
        add_check("interlock", True, "[dry-run] THROTTLE 5 should return ERR NOT_ARMED")
        add_check("safe", True, "[dry-run] SAFE")
        report["ok"] = True
        print(json.dumps(report, indent=2))
        return 0

    overall_ok = True
    with KartLink(args.port, baud=args.baud, timeout=args.timeout) as link:
        try:
            resp = link.command("PING")
            add_check("ping", True, resp.response)
        except Exception as exc:
            add_check("ping", False, str(exc))
            overall_ok = False

        try:
            resp = link.command("STATUS")
            add_check("status_initial", True, resp.response)
        except Exception as exc:
            add_check("status_initial", False, str(exc))
            overall_ok = False

        # Interlock verification: hazardous command should be rejected when not armed.
        try:
            link.command("THROTTLE 5")
            add_check("interlock_not_armed", False, "THROTTLE 5 unexpectedly accepted while disarmed")
            overall_ok = False
        except KartProtocolError as exc:
            passed = "NOT_ARMED" in str(exc)
            add_check("interlock_not_armed", passed, str(exc))
            overall_ok = overall_ok and passed

        if args.profile == "vehicle":
            # Vehicle profile keeps commands non-propulsive but validates arm/disarm path.
            try:
                arm = link.command(f"ARM {max(args.arm_seconds, 1.0):.2f}")
                add_check("arm_path", True, arm.response)
                disarm = link.command("DISARM")
                add_check("disarm_path", True, disarm.response)
            except Exception as exc:
                add_check("arm_disarm_path", False, str(exc))
                overall_ok = False

        try:
            resp = link.command("SAFE")
            add_check("safe", True, resp.response)
        except Exception as exc:
            add_check("safe", False, str(exc))
            overall_ok = False

        try:
            resp = link.command("STATUS")
            add_check("status_final", True, resp.response)
        except Exception as exc:
            add_check("status_final", False, str(exc))
            overall_ok = False

    report["ok"] = overall_ok
    print(json.dumps(report, indent=2))
    return 0 if overall_ok else 2


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        if args.command == "validate":
            if args.validate_command == "bringup":
                return run_validate_bringup(args)
            raise ValueError(f"Unsupported validate subcommand: {args.validate_command}")
        return run_single(args)
    except (KartConnectionError, KartProtocolError, KartTimeoutError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
