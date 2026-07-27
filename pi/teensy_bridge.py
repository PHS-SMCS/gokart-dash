#!/usr/bin/env python3
"""HTTP + WebSocket bridge between the dashboard browser and the Teensy.

The Teensy shares one UART (`/dev/serial0`) between two interleaved channels
(see docs/protocols/uart-protocol.md):

  * an ASCII command channel — `STATUS`, `LED …`, `OK/ERR/INFO` replies, and
  * a 20 Hz binary telemetry stream — `0xF7`-framed `TELEMETRY_V1` packets.

A single reader thread owns the port and demuxes the two: binary telemetry
frames are decoded and pushed to every connected dashboard over a WebSocket at
`/telemetry` (the shape the dash `wire.ts` expects); ASCII `OK/ERR` lines are
routed back to whoever issued a command. Writes (commands) are serialized behind
the same manager so the LED / STATUS endpoints keep working.

Endpoints:
    GET  /telemetry              -> WebSocket, 20 Hz JSON telemetry frames
    GET  /api/health             -> {"ok": true, "serial": "open"|"closed"}
    GET  /api/status             -> parsed STATUS dict
    POST /api/led {r,g,b}        -> sends `LED <r> <g> <b>` (solid color)
    POST /api/led {effect}       -> sends `LED <EFFECT>` (Teensy runs the effect)
    GET  /api/gps                -> latest NEO-M9N snapshot (I2C)
"""

from __future__ import annotations

import base64
import hashlib
import json
import logging
import os
import queue
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial  # python3-serial

try:
    from smbus2 import SMBus, i2c_msg
except ImportError:  # pragma: no cover
    SMBus = None  # type: ignore
    i2c_msg = None  # type: ignore

LOG = logging.getLogger("teensy_bridge")

DEVICE = os.environ.get("TEENSY_DEVICE", "/dev/serial0")
BAUD = int(os.environ.get("TEENSY_BAUD", "115200"))
HOST = os.environ.get("BRIDGE_HOST", "127.0.0.1")
PORT = int(os.environ.get("BRIDGE_PORT", "5174"))
RESPONSE_TIMEOUT_S = 1.0

# STATUS is polled at this rate purely to enrich the binary telemetry stream
# with the few fields the binary frame does not carry (gear from `speed=`, and
# the contactor precharge phase from `bus=`). Everything else on the dash comes
# from the 20 Hz binary frames.
STATUS_POLL_HZ = float(os.environ.get("STATUS_POLL_HZ", "4"))

# SPD pulse-rate -> road speed. The Teensy's hall_hz_x10 now carries the ESC
# SPD line (pin 22), not the useless pin-2 hall (see docs/speedometer.md).
# mph = SPD_hz * (wheel_circumference) / (SPD pulses per wheel revolution).
# PROVISIONAL geometry estimate (assumes 6 pulses/motor-rev x 8.37 drivetrain
# reduction = ~50.2 pulses/wheel-rev, 16" tyre => ~50.3" circumference):
#   50.27 in / 50.2 pulses * 3600 s/h / 63360 in/mi = ~0.057 mph/Hz.
# NEEDS road calibration (drive a measured distance, or count wheel revs at a
# steady speed); tune HALL_MPH_PER_HZ via env once measured. Until then the dial
# moves proportionally to real speed but the magnitude is unverified.
HALL_MPH_PER_HZ = float(os.environ.get("HALL_MPH_PER_HZ", "0.057"))

GPS_BUS = int(os.environ.get("GPS_I2C_BUS", "1"))
GPS_ADDR = int(os.environ.get("GPS_I2C_ADDR", "0x42"), 0)
GPS_POLL_HZ = float(os.environ.get("GPS_POLL_HZ", "5"))

# ---- binary telemetry framing (uart-protocol.md §2) ----
FRAME_SYNC = 0xF7
TYPE_TELEMETRY_V1 = 0x01
TYPE_EVENT = 0x02
TELEM_PAYLOAD_LEN = 30
# `<BBBHBBhhIHHhhbbIB` = proto,state,fault,flags,thr,brk,steerSet,steerMeas,
# hall_count,hall_hz_x10,batt_dv,batt_da,esc_rpm,ctrl_temp,motor_temp,uptime,seq
TELEM_STRUCT = struct.Struct("<BBBHBBhhIHHhhbbIB")
assert TELEM_STRUCT.size == TELEM_PAYLOAD_LEN

FLAG_WHEEL = 1 << 0
FLAG_STEER_LINK = 1 << 1
FLAG_STEER_CAL = 1 << 2
FLAG_ESC_LINK = 1 << 3
FLAG_CONTACTOR = 1 << 4
FLAG_REVERSE = 1 << 5
FLAG_BRAKE = 1 << 6
FLAG_RC_LINK = 1 << 7
FLAG_BENCH = 1 << 8
FLAG_PARK = 1 << 9

TEMP_UNKNOWN = -128


def crc16_ccitt_false(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection/xorout)."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# =====================================================================
# WebSocket hub (hand-rolled server->client text frames; stdlib only)
# =====================================================================

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def ws_accept_key(client_key: str) -> str:
    digest = hashlib.sha1((client_key + WS_GUID).encode()).digest()
    return base64.b64encode(digest).decode()


def ws_encode_text(payload: bytes) -> bytes:
    """Frame a server->client TEXT message (FIN=1, unmasked)."""
    header = bytearray([0x81])
    n = len(payload)
    if n < 126:
        header.append(n)
    elif n < 65536:
        header.append(126)
        header += struct.pack(">H", n)
    else:
        header.append(127)
        header += struct.pack(">Q", n)
    return bytes(header) + payload


class WsClient:
    """One connected dashboard socket. Sends are serialized by its own lock."""

    def __init__(self, wfile):
        self._wfile = wfile
        self._lock = threading.Lock()
        self.alive = True

    def send(self, frame: bytes) -> None:
        if not self.alive:
            return
        with self._lock:
            try:
                self._wfile.write(frame)
                self._wfile.flush()
            except (OSError, ValueError):
                self.alive = False


class WsHub:
    def __init__(self):
        self._clients: set[WsClient] = set()
        self._lock = threading.Lock()

    def add(self, client: WsClient) -> None:
        with self._lock:
            self._clients.add(client)
        LOG.info("telemetry ws: client connected (%d total)", self.count())

    def remove(self, client: WsClient) -> None:
        with self._lock:
            self._clients.discard(client)
        LOG.info("telemetry ws: client gone (%d total)", self.count())

    def count(self) -> int:
        with self._lock:
            return len(self._clients)

    def broadcast_json(self, obj: dict) -> None:
        with self._lock:
            if not self._clients:
                return
            clients = list(self._clients)
        frame = ws_encode_text(json.dumps(obj, separators=(",", ":")).encode())
        dead = []
        for c in clients:
            c.send(frame)
            if not c.alive:
                dead.append(c)
        if dead:
            with self._lock:
                self._clients.difference_update(dead)


# =====================================================================
# Serial manager: owns the port, demuxes ASCII + binary, broadcasts telemetry
# =====================================================================


class SerialManager:
    def __init__(self, device: str, baud: int, hub: WsHub, gps: "GpsReader"):
        self._device = device
        self._baud = baud
        self._hub = hub
        self._gps = gps
        self._ser: serial.Serial | None = None
        self._write_lock = threading.Lock()
        self._replies: "queue.Queue[str]" = queue.Queue(maxsize=64)
        self._buf = bytearray()
        # Fields the binary frame lacks, refreshed by the STATUS poller.
        self._enrich_lock = threading.Lock()
        self._gear = "LOW"
        self._bus_phase = "open"
        threading.Thread(target=self._reader_loop, name="serial-reader", daemon=True).start()

    # ---- port lifecycle ----
    def _open(self) -> serial.Serial:
        if self._ser is None or not self._ser.is_open:
            self._ser = serial.Serial(self._device, baudrate=self._baud, timeout=0.1)
        return self._ser

    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    # ---- reader thread ----
    def _reader_loop(self) -> None:
        while True:
            try:
                ser = self._open()
                chunk = ser.read(512)
                if chunk:
                    self._buf.extend(chunk)
                    self._process_buffer()
            except (serial.SerialException, OSError) as exc:
                LOG.warning("serial reader: %s; reopening in 1s", exc)
                try:
                    if self._ser is not None:
                        self._ser.close()
                except Exception:  # noqa: BLE001
                    pass
                self._ser = None
                time.sleep(1.0)

    def _process_buffer(self) -> None:
        buf = self._buf
        while buf:
            b0 = buf[0]
            if b0 == FRAME_SYNC:
                if len(buf) < 4:
                    break  # need at least sync+len+type+...
                length = buf[1]  # = 1 (type) + payload_len
                if length < 1 or length > 64:
                    del buf[0]  # implausible LEN -> resync
                    continue
                frame_len = 2 + length + 2  # sync,len + (type+payload) + crc16
                if len(buf) < frame_len:
                    break  # wait for the rest of the frame
                body = bytes(buf[1 : 2 + length])  # LEN..PAYLOAD (CRC input)
                crc_rx = buf[2 + length] | (buf[3 + length] << 8)
                if crc16_ccitt_false(body) == crc_rx:
                    self._handle_frame(buf[2], bytes(buf[3 : 2 + length]))
                    del buf[:frame_len]
                else:
                    del buf[0]  # bad CRC -> resync one byte
                continue
            # ASCII channel: consume up to the next newline, but never past a
            # binary sync byte (0xF7 never appears in the ASCII channel, so one
            # showing up means we are mis-aligned — drop the garbage before it).
            nl = buf.find(0x0A)
            sync = buf.find(FRAME_SYNC)
            if sync != -1 and (nl == -1 or sync < nl):
                del buf[:sync]
                continue
            if nl == -1:
                if len(buf) > 512:  # runaway partial line without a sync -> trim
                    del buf[: len(buf) - 64]
                break
            line = bytes(buf[:nl]).decode("ascii", errors="replace").strip("\r").strip()
            del buf[: nl + 1]
            if line:
                self._handle_ascii(line)

    def _handle_ascii(self, line: str) -> None:
        if line.startswith("OK") or line.startswith("ERR"):
            try:
                self._replies.put_nowait(line)
            except queue.Full:
                # Drop the oldest so a stalled command can't wedge the queue.
                try:
                    self._replies.get_nowait()
                    self._replies.put_nowait(line)
                except queue.Empty:
                    pass
        # INFO / async lines are not command replies; the notification layer
        # derives events from telemetry state, so we just log them.
        elif line.startswith("INFO"):
            LOG.debug("teensy: %s", line)

    def _handle_frame(self, ftype: int, payload: bytes) -> None:
        if ftype != TYPE_TELEMETRY_V1 or len(payload) != TELEM_PAYLOAD_LEN:
            return
        (proto, state, fault, flags, thr, brk, steer_set, steer_meas,
         _hall_count, hall_hz_x10, batt_dv, batt_da, esc_rpm,
         ctrl_temp, motor_temp, uptime, seq) = TELEM_STRUCT.unpack(payload)
        if proto != 1:
            return
        self._hub.broadcast_json(self._build_wire(
            state, fault, flags, thr, brk, steer_set, steer_meas,
            hall_hz_x10, batt_dv, batt_da, esc_rpm, ctrl_temp, motor_temp,
            uptime, seq))

    def _build_wire(self, state, fault, flags, thr, brk, steer_set, steer_meas,
                    hall_hz_x10, batt_dv, batt_da, esc_rpm, ctrl_temp,
                    motor_temp, uptime, seq) -> dict:
        with self._enrich_lock:
            gear = self._gear
            bus_phase = self._bus_phase

        has_esc = batt_dv > 0  # ESC/BMS telemetry only present once keyed on
        if fault == 8:
            contactor = "FAULT"
        elif bus_phase in ("precharge", "settling"):
            contactor = "PRECHARGE"
        elif bus_phase == "closed" or (flags & FLAG_CONTACTOR):
            contactor = "CLOSED"
        else:
            contactor = "OPEN"

        return {
            "type": "telemetry",
            "t": uptime,
            "seq": seq,
            "state": state,
            "fault": fault,
            "gear": gear,
            "throttle": thr,
            "brake": brk,
            "speedMph": round(hall_hz_x10 / 10.0 * HALL_MPH_PER_HZ, 2),
            "steerSet": steer_set / 100.0,
            "steerMeas": steer_meas / 100.0,
            "contactor": contactor,
            "flags": {
                "wheel": bool(flags & FLAG_WHEEL),
                "steerLink": bool(flags & FLAG_STEER_LINK),
                "steerCal": bool(flags & FLAG_STEER_CAL),
                "escLink": bool(flags & FLAG_ESC_LINK),
                "contactor": bool(flags & FLAG_CONTACTOR),
                "reverse": bool(flags & FLAG_REVERSE),
                "park": bool(flags & FLAG_PARK),
                "brake": bool(flags & FLAG_BRAKE),
                "bench": bool(flags & FLAG_BENCH),
            },
            "battV": round(batt_dv / 10.0, 1) if has_esc else None,
            "battA": round(batt_da / 10.0, 1) if has_esc else None,
            "escRpm": esc_rpm if has_esc else None,
            "ctrlTempC": None if ctrl_temp == TEMP_UNKNOWN else ctrl_temp,
            "motorTempC": None if motor_temp == TEMP_UNKNOWN else motor_temp,
            "gps": self._gps.wire_snapshot(),
        }

    def update_enrichment(self, status: dict) -> None:
        # `speed=` carries the shift-ladder rung; Park/Reverse hold the ESC in
        # LOW, and P/R display is driven by the telemetry flags, so `gear` here
        # is just the ESC speed gear (LOW/MED/HIGH).
        speed = str(status.get("speed", "")).upper()
        gear = speed if speed in ("LOW", "MED", "HIGH") else "LOW"
        bus = str(status.get("bus", "open")).lower()
        with self._enrich_lock:
            self._gear = gear
            self._bus_phase = bus

    # ---- command channel (write + await OK/ERR) ----
    def send(self, command: str, timeout_s: float = RESPONSE_TIMEOUT_S) -> str:
        with self._write_lock:
            ser = self._open()
            # Drain any stale replies so we match this command's response.
            while True:
                try:
                    self._replies.get_nowait()
                except queue.Empty:
                    break
            try:
                ser.write((command.strip() + "\n").encode())
                ser.flush()
            except (serial.SerialException, OSError) as exc:
                self._ser = None
                raise RuntimeError(f"serial: {exc}") from exc
            try:
                return self._replies.get(timeout=timeout_s)
            except queue.Empty:
                raise TimeoutError(f"no OK/ERR for {command!r}")


class StatusPoller:
    """Low-rate STATUS poll: enriches the telemetry stream with gear + bus
    phase (fields the binary frame does not carry)."""

    def __init__(self, link: "SerialManager", hz: float):
        self._link = link
        self._period = max(0.1, 1.0 / max(hz, 0.1))
        threading.Thread(target=self._run, name="status-poller", daemon=True).start()

    def _run(self) -> None:
        while True:
            time.sleep(self._period)
            try:
                line = self._link.send("STATUS")
                self._link.update_enrichment(parse_status(line))
            except Exception as exc:  # noqa: BLE001
                LOG.debug("status poll: %s", exc)


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

    def wire_snapshot(self) -> dict:
        """The subset the dashboard telemetry frame carries (wire.ts)."""
        with self._lock:
            s = self._snapshot
            return {
                "fix": bool(s["fix"]),
                "lat": s["lat"],
                "lon": s["lon"],
                "heading": s["heading_deg"],
                "sats": s["sats"],
                "speedKph": s["speed_kph"],
            }

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
                            # Drain the DDC stream in bulk so the read rate keeps
                            # up with the NMEA volume. One-byte-at-a-time reads
                            # (read_byte_data) are far too slow and let the
                            # receiver's buffer back up, which drops sentences and
                            # makes the fix go stale. Read register 0xFF in blocks
                            # via i2c_rdwr, capped per poll so a huge backlog can't
                            # monopolise the bus.
                            remaining = min(avail, 4096)
                            got = False
                            while remaining > 0:
                                n = min(remaining, 256)
                                try:
                                    wr = i2c_msg.write(self._address, [0xFF])
                                    rd = i2c_msg.read(self._address, n)
                                    bus.i2c_rdwr(wr, rd)
                                    chunk = bytes(rd)
                                except OSError as exc:
                                    self._set_error(f"read stream: {exc}")
                                    break
                                if not chunk:
                                    break
                                buf.extend(chunk)
                                remaining -= len(chunk)
                                got = True
                            if got:
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
    server_version = "GoKartBridge/2"
    protocol_version = "HTTP/1.1"

    # Shared services; assigned in main().
    link: SerialManager
    gps: "GpsReader"
    hub: WsHub

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

    # ---- WebSocket telemetry ----
    def _serve_telemetry_ws(self) -> None:
        key = self.headers.get("Sec-WebSocket-Key")
        upgrade = (self.headers.get("Upgrade") or "").lower()
        if not key or upgrade != "websocket":
            self._send_json(400, {"ok": False, "error": "expected websocket upgrade"})
            return
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", ws_accept_key(key))
        self.end_headers()
        try:
            self.wfile.flush()
        except OSError:
            return

        client = WsClient(self.wfile)
        self.hub.add(client)
        try:
            # Keep this connection's thread alive reading client control frames
            # (close/ping); broadcasts are pushed from the serial reader thread.
            self._ws_read_loop(client)
        finally:
            client.alive = False
            self.hub.remove(client)

    def _ws_read_loop(self, client: WsClient) -> None:
        rfile = self.rfile
        while client.alive:
            hdr = rfile.read(2)
            if len(hdr) < 2:
                return
            opcode = hdr[0] & 0x0F
            masked = hdr[1] & 0x80
            length = hdr[1] & 0x7F
            if length == 126:
                ext = rfile.read(2)
                if len(ext) < 2:
                    return
                length = struct.unpack(">H", ext)[0]
            elif length == 127:
                ext = rfile.read(8)
                if len(ext) < 8:
                    return
                length = struct.unpack(">Q", ext)[0]
            mask = rfile.read(4) if masked else b"\x00\x00\x00\x00"
            data = rfile.read(length) if length else b""
            if masked and length:
                data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
            if opcode == 0x8:  # close
                return
            if opcode == 0x9:  # ping -> pong
                client.send(bytes([0x8A, len(data) & 0x7F]) + data)

    def do_GET(self):
        if self.path == "/telemetry":
            self._serve_telemetry_ws()
            return
        if self.path == "/api/health":
            self._send_json(200, {"ok": True,
                                  "serial": "open" if self.link.is_open() else "closed",
                                  "clients": self.hub.count()})
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

    hub = WsHub()
    gps = GpsReader(GPS_BUS, GPS_ADDR, GPS_POLL_HZ)
    gps.start()
    link = SerialManager(DEVICE, BAUD, hub, gps)
    StatusPoller(link, STATUS_POLL_HZ)

    Handler.link = link
    Handler.gps = gps
    Handler.hub = hub
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    LOG.info("listening on http://%s:%d  (ws /telemetry; serial=%s @ %d, gps=i2c-%d/0x%02X)",
             HOST, PORT, DEVICE, BAUD, GPS_BUS, GPS_ADDR)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
