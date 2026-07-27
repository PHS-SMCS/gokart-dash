// Wire contract between the Pi bridge (`kartd` / teensy_bridge.py) and the
// dashboard. The bridge parses the Teensy binary telemetry frames
// (uart-protocol.md TYPE 0x01) and re-emits them as compact JSON over the
// telemetry WebSocket at 20 Hz. This module is the single place that knows
// that JSON shape, so the rest of the UI only ever sees `Telemetry`.

import type {
  ContactorPhase,
  DriveState,
  SpeedGear,
  Telemetry,
} from './types';
import { DISCONNECTED } from './types';

/** One telemetry frame as sent by the bridge (all fields optional/defensive). */
export interface WireFrame {
  type?: 'telemetry';
  t?: number; // uptime_ms
  seq?: number;
  state?: DriveState | number;
  fault?: number;
  gear?: SpeedGear;
  reverse?: boolean;
  throttle?: number;
  brake?: number;
  speedMph?: number;
  steerSet?: number;
  steerMeas?: number;
  contactor?: ContactorPhase;
  flags?: {
    wheel?: boolean;
    steerLink?: boolean;
    steerCal?: boolean;
    escLink?: boolean;
    contactor?: boolean;
    reverse?: boolean;
    park?: boolean;
    brake?: boolean;
    bench?: boolean;
  };
  battV?: number | null;
  battA?: number | null;
  escRpm?: number | null;
  ctrlTempC?: number | null;
  motorTempC?: number | null;
  gps?: {
    fix?: boolean;
    lat?: number | null;
    lon?: number | null;
    heading?: number | null;
    sats?: number;
    speedKph?: number | null;
  };
}

const STATE_BY_INDEX: DriveState[] = ['SAFE', 'ARMED', 'DRIVE', 'STOPPING', 'FAULT'];

function decodeState(s: WireFrame['state']): DriveState {
  if (typeof s === 'number') return STATE_BY_INDEX[s] ?? 'SAFE';
  if (s && STATE_BY_INDEX.includes(s)) return s;
  return 'SAFE';
}

function num(v: unknown, fallback: number): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback;
}

function nullableNum(v: unknown): number | null {
  return typeof v === 'number' && Number.isFinite(v) ? v : null;
}

/**
 * Fold a wire frame onto the previous telemetry. Missing fields keep their
 * prior value (frames may be partial), except the link block, which the
 * caller owns.
 */
export function applyFrame(prev: Telemetry, f: WireFrame): Telemetry {
  const flags = f.flags ?? {};
  const contactor: ContactorPhase =
    f.contactor ??
    (num(f.fault, prev.faultCode) === 8
      ? 'FAULT'
      : (flags.contactor ?? prev.contactor === 'CLOSED')
        ? 'CLOSED'
        : 'OPEN');

  return {
    ...prev,
    driveState: f.state !== undefined ? decodeState(f.state) : prev.driveState,
    faultCode: num(f.fault, prev.faultCode),
    gear: f.gear ?? prev.gear,
    reverse: flags.reverse ?? f.reverse ?? prev.reverse,
    parked: flags.park ?? prev.parked,
    speedMph: num(f.speedMph, prev.speedMph),
    throttlePct: num(f.throttle, prev.throttlePct),
    brakePct: num(f.brake, prev.brakePct),
    steerSetpointDeg: num(f.steerSet, prev.steerSetpointDeg),
    steerMeasuredDeg: num(f.steerMeas, prev.steerMeasuredDeg),
    wheelConnected: flags.wheel ?? prev.wheelConnected,
    steerLinkOk: flags.steerLink ?? prev.steerLinkOk,
    steerCalibrated: flags.steerCal ?? prev.steerCalibrated,
    escLinkOk: flags.escLink ?? prev.escLinkOk,
    contactor,
    brakeActive: flags.brake ?? prev.brakeActive,
    benchMode: flags.bench ?? prev.benchMode,
    battVolts: f.battV !== undefined ? nullableNum(f.battV) : prev.battVolts,
    battAmps: f.battA !== undefined ? nullableNum(f.battA) : prev.battAmps,
    escRpm: f.escRpm !== undefined ? nullableNum(f.escRpm) : prev.escRpm,
    controllerTempC:
      f.ctrlTempC !== undefined ? nullableNum(f.ctrlTempC) : prev.controllerTempC,
    motorTempC:
      f.motorTempC !== undefined ? nullableNum(f.motorTempC) : prev.motorTempC,
    uptimeMs: num(f.t, prev.uptimeMs),
    seq: num(f.seq, prev.seq),
    gps: f.gps
      ? {
          fix: f.gps.fix ?? prev.gps.fix,
          lat: f.gps.lat !== undefined ? nullableNum(f.gps.lat) : prev.gps.lat,
          lon: f.gps.lon !== undefined ? nullableNum(f.gps.lon) : prev.gps.lon,
          headingDeg:
            f.gps.heading !== undefined ? nullableNum(f.gps.heading) : prev.gps.headingDeg,
          sats: num(f.gps.sats, prev.gps.sats),
          speedKph:
            f.gps.speedKph !== undefined ? nullableNum(f.gps.speedKph) : prev.gps.speedKph,
        }
      : prev.gps,
    link: prev.link,
  };
}

export function decodeFrame(prev: Telemetry, raw: string): Telemetry | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return null;
  }
  if (!parsed || typeof parsed !== 'object') return null;
  const f = parsed as WireFrame;
  // Events are handled by the notification layer, not the telemetry state.
  if (f.type && f.type !== 'telemetry') return null;
  return applyFrame(prev, f);
}

/** The bridge base URL — same host as the dashboard, fixed bridge port. */
export const BRIDGE_PORT = 5174;

export function telemetryWsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${window.location.hostname}:${BRIDGE_PORT}/telemetry`;
}

export { DISCONNECTED };
