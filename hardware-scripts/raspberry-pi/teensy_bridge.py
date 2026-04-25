#!/usr/bin/env python3
"""HTTP bridge between the dashboard browser and the Teensy kart controller.

Exposes a tiny localhost-only JSON API. Each endpoint serializes a single
command to the Teensy over /dev/serial0 and returns the firmware's reply.

Endpoints:
    GET  /api/health             -> {"ok": true, "serial": "open"|"closed"}
    GET  /api/status             -> parsed STATUS dict
    POST /api/led {r,g,b}        -> sends `LED <r> <g> <b>`
"""

from __future__ import annotations

import json
import logging
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial  # python3-serial

LOG = logging.getLogger("teensy_bridge")

DEVICE = os.environ.get("TEENSY_DEVICE", "/dev/serial0")
BAUD = int(os.environ.get("TEENSY_BAUD", "115200"))
HOST = os.environ.get("BRIDGE_HOST", "127.0.0.1")
PORT = int(os.environ.get("BRIDGE_PORT", "5174"))
RESPONSE_TIMEOUT_S = 1.0


class TeensyLink:
    """Thread-safe single-connection wrapper with auto-reopen."""

    def __init__(self, device: str, baud: int):
        self._device = device
        self._baud = baud
        self._lock = threading.Lock()
        self._ser: serial.Serial | None = None

    def _open(self) -> serial.Serial:
        if self._ser is None or not self._ser.is_open:
            self._ser = serial.Serial(self._device, baudrate=self._baud, timeout=0.2)
            time.sleep(0.05)
            self._ser.reset_input_buffer()
        return self._ser

    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def send(self, command: str, timeout_s: float = RESPONSE_TIMEOUT_S) -> str:
        """Send one line, return the first OK/ERR line. Raises on failure."""
        with self._lock:
            try:
                ser = self._open()
                ser.reset_input_buffer()
                ser.write((command.strip() + "\n").encode())
                ser.flush()

                deadline = time.monotonic() + timeout_s
                last = ""
                while time.monotonic() < deadline:
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode(errors="replace").strip()
                    if not line:
                        continue
                    last = line
                    if line.startswith("OK") or line.startswith("ERR"):
                        return line
                raise TimeoutError(f"no OK/ERR for {command!r}; last={last!r}")
            except (serial.SerialException, OSError) as exc:
                # Force a reopen on the next call.
                if self._ser is not None:
                    try:
                        self._ser.close()
                    except Exception:
                        pass
                self._ser = None
                raise RuntimeError(f"serial: {exc}") from exc


def parse_status(line: str) -> dict:
    """Turn `OK STATUS k=v k=v ...` into a dict."""
    if not line.startswith("OK STATUS"):
        return {"raw": line}
    out: dict[str, str | float | int | list[int]] = {}
    for tok in line.split()[2:]:
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        if "," in v:
            try:
                out[k] = [int(x) for x in v.split(",")]
                continue
            except ValueError:
                pass
        try:
            out[k] = int(v)
            continue
        except ValueError:
            pass
        try:
            out[k] = float(v)
            continue
        except ValueError:
            pass
        out[k] = v
    return out


def clamp_byte(v) -> int:
    n = int(v)
    if n < 0:
        return 0
    if n > 255:
        return 255
    return n


class Handler(BaseHTTPRequestHandler):
    server_version = "GoKartBridge/1"

    # Single shared link; assigned in main().
    link: TeensyLink

    def log_message(self, fmt, *args):
        LOG.info("%s - %s", self.address_string(), fmt % args)

    def _send_json(self, status: int, payload: dict):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        # Localhost-only service; the dashboard fetches from a sibling port.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", "0") or 0)
        if length <= 0 or length > 4096:
            return {}
        raw = self.rfile.read(length)
        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            return {}
        return data if isinstance(data, dict) else {}

    def do_OPTIONS(self):
        self._send_json(204, {})

    def do_GET(self):
        if self.path == "/api/health":
            self._send_json(200, {"ok": True, "serial": "open" if self.link.is_open() else "closed"})
            return
        if self.path == "/api/status":
            try:
                line = self.link.send("STATUS")
            except Exception as exc:
                self._send_json(503, {"ok": False, "error": str(exc)})
                return
            self._send_json(200, {"ok": True, "status": parse_status(line), "raw": line})
            return
        self._send_json(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        if self.path == "/api/led":
            data = self._read_json()
            try:
                r = clamp_byte(data.get("r", 0))
                g = clamp_byte(data.get("g", 0))
                b = clamp_byte(data.get("b", 0))
            except (TypeError, ValueError):
                self._send_json(400, {"ok": False, "error": "r,g,b must be integers"})
                return
            try:
                line = self.link.send(f"LED {r} {g} {b}")
            except Exception as exc:
                self._send_json(503, {"ok": False, "error": str(exc), "rgb": [r, g, b]})
                return
            ok = line.startswith("OK")
            self._send_json(200 if ok else 502, {"ok": ok, "rgb": [r, g, b], "raw": line})
            return
        self._send_json(404, {"ok": False, "error": "not found"})


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        stream=sys.stderr,
    )

    Handler.link = TeensyLink(DEVICE, BAUD)
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    LOG.info("listening on http://%s:%d  (serial=%s @ %d)", HOST, PORT, DEVICE, BAUD)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
