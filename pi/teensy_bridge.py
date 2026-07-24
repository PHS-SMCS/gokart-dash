#!/usr/bin/env python3
"""HTTP bridge between the dashboard browser and the Teensy kart controller.

Exposes a tiny localhost-only JSON API. Each endpoint serializes a single
command to the Teensy over /dev/serial0 and returns the firmware's reply.

Endpoints:
    GET  /api/health             -> {"ok": true, "serial": "open"|"closed"}
    GET  /api/status             -> parsed STATUS dict
    POST /api/led {r,g,b}        -> sends `LED <r> <g> <b>` (solid color)
    POST /api/led {effect}       -> sends `LED <EFFECT>` (Teensy runs the effect)
    GET  /api/gps                -> latest NEO-M9N snapshot (I2C)
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

try:
    from smbus2 import SMBus
except ImportError:  # pragma: no cover
    SMBus = None  # type: ignore

LOG = logging.getLogger("teensy_bridge")

DEVICE = os.environ.get("TEENSY_DEVICE", "/dev/serial0")
BAUD = int(os.environ.get("TEENSY_BAUD", "115200"))
HOST = os.environ.get("BRIDGE_HOST", "127.0.0.1")
PORT = int(os.environ.get("BRIDGE_PORT", "5174"))
RESPONSE_TIMEOUT_S = 1.0

GPS_BUS = int(os.environ.get("GPS_I2C_BUS", "1"))
GPS_ADDR = int(os.environ.get("GPS_I2C_ADDR", "0x42"), 0)
GPS_POLL_HZ = float(os.environ.get("GPS_POLL_HZ", "5"))


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


class GpsReader:
    """Background NEO-M9N reader. Polls the u-blox DDC interface, parses
    NMEA RMC/GGA, and keeps a thread-safe snapshot of the latest fix.

    The receiver streams continuously even with no fix — the snapshot
    reflects that by exposing both a `stream_ok` flag (bytes recently
    arriving) and a `fix` flag (RMC status A or GGA quality > 0).
    """

    def __init__(self, bus_id: int, address: int, poll_hz: float):
        self._bus_id = bus_id
        self._address = address
        self._period = max(0.05, 1.0 / max(poll_hz, 0.1))
        self._lock = threading.Lock()
        self._snapshot: dict = {
            "fix": False,
            "lat": None,
            "lon": None,
            "alt_m": None,
            "sats": 0,
            "hdop": None,
            "speed_kph": None,
            "heading_deg": None,
            "utc": None,
            "updated_mono": 0.0,
            "last_byte_mono": 0.0,
            "stream_ok": False,
            "error": None,
        }
        self._thread = threading.Thread(target=self._run, name="gps-reader", daemon=True)

    def start(self) -> None:
        if SMBus is None:
            with self._lock:
                self._snapshot["error"] = "smbus2 not installed"
            LOG.warning("smbus2 unavailable; GPS reader disabled")
            return
        self._thread.start()

    def snapshot(self) -> dict:
        now = time.monotonic()
        with self._lock:
            snap = dict(self._snapshot)
        updated = snap.pop("updated_mono") or 0.0
        last_byte = snap.pop("last_byte_mono") or 0.0
        snap["age_ms"] = int((now - updated) * 1000) if updated > 0 else None
        snap["stream_age_ms"] = int((now - last_byte) * 1000) if last_byte > 0 else None
        # `stream_ok` becomes False if no bytes for >3s even if the thread
        # hasn't noticed yet (e.g. mid-sleep).
        if last_byte > 0 and (now - last_byte) > 3.0:
            snap["stream_ok"] = False
        return snap

    def _run(self) -> None:
        buf = bytearray()
        while True:
            try:
                with SMBus(self._bus_id) as bus:
                    LOG.info("GPS: opened i2c-%d, polling 0x%02X at %.1f Hz",
                             self._bus_id, self._address, 1.0 / self._period)
                    while True:
                        try:
                            hi = bus.read_byte_data(self._address, 0xFD)
                            lo = bus.read_byte_data(self._address, 0xFE)
                        except OSError as exc:
                            self._set_error(f"read avail: {exc}")
                            time.sleep(1.0)
                            break  # reopen the bus
                        avail = (hi << 8) | lo
                        if avail:
                            chunk = bytearray()
                            for _ in range(min(avail, 256)):
                                try:
                                    chunk.append(bus.read_byte_data(self._address, 0xFF))
                                except OSError:
                                    break
                            if chunk:
                                buf.extend(chunk)
                                self._mark_byte()
                                buf = self._consume(buf)
                                # Cap buffer in case of stuck partial line.
                                if len(buf) > 4096:
                                    buf = buf[-1024:]
                        time.sleep(self._period)
            except FileNotFoundError as exc:
                self._set_error(f"bus open: {exc}")
                time.sleep(5.0)
            except Exception as exc:  # noqa: BLE001
                self._set_error(f"gps thread: {exc}")
                time.sleep(2.0)

    def _mark_byte(self) -> None:
        now = time.monotonic()
        with self._lock:
            self._snapshot["last_byte_mono"] = now
            self._snapshot["stream_ok"] = True
            self._snapshot["error"] = None

    def _set_error(self, msg: str) -> None:
        LOG.warning("GPS: %s", msg)
        with self._lock:
            self._snapshot["error"] = msg
            self._snapshot["stream_ok"] = False

    def _consume(self, buf: bytearray) -> bytearray:
        # Split on newline; parse complete lines, keep the trailing partial.
        try:
            text = buf.decode("ascii", errors="replace")
        except Exception:  # noqa: BLE001
            return bytearray()
        # u-blox uses CRLF; split on \n and strip \r.
        pieces = text.split("\n")
        for line in pieces[:-1]:
            self._parse_line(line.strip("\r").strip())
        tail = pieces[-1]
        return bytearray(tail.encode("ascii", errors="ignore"))

    def _parse_line(self, line: str) -> None:
        if not line.startswith("$") or "*" not in line:
            return
        body, _, ck = line.partition("*")
        try:
            expected = int(ck[:2], 16)
        except ValueError:
            return
        actual = 0
        for ch in body[1:]:
            actual ^= ord(ch)
        if actual != expected:
            return
        parts = body[1:].split(",")
        tag = parts[0]
        if len(tag) >= 5 and tag[2:] == "RMC":
            self._parse_rmc(parts)
        elif len(tag) >= 5 and tag[2:] == "GGA":
            self._parse_gga(parts)

    @staticmethod
    def _nmea_to_decimal(value: str, hemi: str):
        if not value or not hemi:
            return None
        try:
            dot = value.index(".")
            deg_len = dot - 2
            if deg_len <= 0:
                return None
            deg = int(value[:deg_len])
            mins = float(value[deg_len:])
            dec = deg + mins / 60.0
            if hemi in ("S", "W"):
                dec = -dec
            return dec
        except (ValueError, IndexError):
            return None

    def _parse_rmc(self, p: list[str]) -> None:
        # $..RMC,utc,status,lat,N/S,lon,E/W,sog_kn,cog,date,...
        if len(p) < 10:
            return
        status = p[2]
        lat = self._nmea_to_decimal(p[3], p[4])
        lon = self._nmea_to_decimal(p[5], p[6])
        try:
            sog_kn = float(p[7]) if p[7] else None
        except ValueError:
            sog_kn = None
        try:
            cog = float(p[8]) if p[8] else None
        except ValueError:
            cog = None
        utc = p[1] or None
        now = time.monotonic()
        with self._lock:
            self._snapshot["fix"] = (status == "A")
            if lat is not None and lon is not None:
                self._snapshot["lat"] = lat
                self._snapshot["lon"] = lon
            self._snapshot["speed_kph"] = sog_kn * 1.852 if sog_kn is not None else None
            self._snapshot["heading_deg"] = cog
            self._snapshot["utc"] = utc
            self._snapshot["updated_mono"] = now

    def _parse_gga(self, p: list[str]) -> None:
        # $..GGA,utc,lat,N/S,lon,E/W,fixQual,numSats,hdop,alt,M,...
        if len(p) < 10:
            return
        try:
            fix_q = int(p[6]) if p[6] else 0
        except ValueError:
            fix_q = 0
        try:
            sats = int(p[7]) if p[7] else 0
        except ValueError:
            sats = 0
        try:
            hdop = float(p[8]) if p[8] else None
        except ValueError:
            hdop = None
        try:
            alt = float(p[9]) if p[9] else None
        except ValueError:
            alt = None
        lat = self._nmea_to_decimal(p[2], p[3])
        lon = self._nmea_to_decimal(p[4], p[5])
        now = time.monotonic()
        with self._lock:
            if fix_q > 0:
                self._snapshot["fix"] = True
            self._snapshot["sats"] = sats
            self._snapshot["hdop"] = hdop
            if alt is not None:
                self._snapshot["alt_m"] = alt
            if lat is not None and lon is not None:
                self._snapshot["lat"] = lat
                self._snapshot["lon"] = lon
            self._snapshot["updated_mono"] = now


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

    # Shared services; assigned in main().
    link: TeensyLink
    gps: "GpsReader"

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
        if self.path == "/api/gps":
            self._send_json(200, {"ok": True, "gps": self.gps.snapshot()})
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

            # Effect mode: one command, the Teensy runs the animation itself.
            effect = data.get("effect")
            if effect is not None:
                name = str(effect).strip().upper()
                if not name.isalnum():
                    self._send_json(400, {"ok": False, "error": "bad effect name"})
                    return
                try:
                    line = self.link.send(f"LED {name}")
                except Exception as exc:
                    self._send_json(503, {"ok": False, "error": str(exc), "effect": name})
                    return
                ok = line.startswith("OK")
                self._send_json(200 if ok else 502, {"ok": ok, "effect": name, "raw": line})
                return

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
    Handler.gps = GpsReader(GPS_BUS, GPS_ADDR, GPS_POLL_HZ)
    Handler.gps.start()
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    LOG.info("listening on http://%s:%d  (serial=%s @ %d, gps=i2c-%d/0x%02X)",
             HOST, PORT, DEVICE, BAUD, GPS_BUS, GPS_ADDR)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
