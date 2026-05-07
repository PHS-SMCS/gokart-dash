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
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable, Dict, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kart_link import KartLink, parse_status  # noqa: E402

LOG = logging.getLogger("teensy_bridge")

DEVICE = os.environ.get("TEENSY_DEVICE", "/dev/serial0")
BAUD = int(os.environ.get("TEENSY_BAUD", "115200"))
HOST = os.environ.get("BRIDGE_HOST", "127.0.0.1")
PORT = int(os.environ.get("BRIDGE_PORT", "5174"))
RESPONSE_TIMEOUT_S = 1.0


def clamp_byte(v) -> int:
    n = int(v)
    if n < 0:
        return 0
    if n > 255:
        return 255
    return n


# --- Route handlers -----------------------------------------------------------

def _handle_health(handler: "Handler") -> None:
    handler._send_json(200, {"ok": True, "serial": "open" if handler.link.is_open else "closed"})


def _handle_status(handler: "Handler") -> None:
    try:
        line = handler.link.send_line("STATUS", timeout=RESPONSE_TIMEOUT_S)
    except Exception as exc:
        handler._send_json(503, {"ok": False, "error": str(exc)})
        return
    handler._send_json(200, {"ok": True, "status": parse_status(line), "raw": line})


def _handle_led(handler: "Handler") -> None:
    data = handler._read_json()
    try:
        r = clamp_byte(data.get("r", 0))
        g = clamp_byte(data.get("g", 0))
        b = clamp_byte(data.get("b", 0))
    except (TypeError, ValueError):
        handler._send_json(400, {"ok": False, "error": "r,g,b must be integers"})
        return
    try:
        line = handler.link.send_line(f"LED {r} {g} {b}", timeout=RESPONSE_TIMEOUT_S)
    except Exception as exc:
        handler._send_json(503, {"ok": False, "error": str(exc), "rgb": [r, g, b]})
        return
    ok = line.startswith("OK")
    handler._send_json(200 if ok else 502, {"ok": ok, "rgb": [r, g, b], "raw": line})


ROUTES: Dict[Tuple[str, str], Callable[["Handler"], None]] = {
    ("GET",  "/api/health"): _handle_health,
    ("GET",  "/api/status"): _handle_status,
    ("POST", "/api/led"):    _handle_led,
}


# --- HTTP plumbing ------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "GoKartBridge/1"

    # Single shared link; assigned in main().
    link: KartLink

    def log_message(self, fmt, *args):
        LOG.info("%s - %s", self.address_string(), fmt % args)

    def _send_json(self, status: int, payload: dict) -> None:
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

    def _dispatch(self, method: str) -> None:
        handler = ROUTES.get((method, self.path))
        if handler is None:
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        handler(self)

    def do_OPTIONS(self):
        self._send_json(204, {})

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        stream=sys.stderr,
    )

    Handler.link = KartLink(DEVICE, baud=BAUD, timeout=RESPONSE_TIMEOUT_S)
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    LOG.info("listening on http://%s:%d  (serial=%s @ %d)", HOST, PORT, DEVICE, BAUD)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
