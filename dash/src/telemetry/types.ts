// Telemetry model — mirrors the Teensy binary telemetry frame
// (docs/protocols/uart-protocol.md, TYPE 0x01 TELEMETRY_V1) plus the merged
// GPS/BMS fields the Pi `kartd` bridge folds into the same stream.
//
// There are no sample values in this file. The dashboard is either fed by a
// real source (the live WebSocket bridge) or by the operator-driven simulator
// on the System tab. When neither is present, telemetry sits in DISCONNECTED.

export type DriveState = 'SAFE' | 'ARMED' | 'DRIVE' | 'STOPPING' | 'FAULT';

/** Firmware open-loop speed-gear model (paddle shift). */
export type SpeedGear = 'LOW' | 'MED' | 'HIGH';

/** The selector position the driver sees on the dash gear stack. */
export type GearSelector = 'P' | 'L' | 'M' | 'H' | 'R';

/**
 * Contactor phase. The binary frame only carries CONTACTOR_CLOSED today; the
 * bridge derives PRECHARGE/FAULT from events + fault codes as those land.
 */
export type ContactorPhase = 'OPEN' | 'PRECHARGE' | 'CLOSED' | 'FAULT';

/** Where the current telemetry snapshot is coming from. */
export type TelemetrySource = 'none' | 'live' | 'sim';

export interface GpsFix {
  fix: boolean;
  lat: number | null;
  lon: number | null;
  headingDeg: number | null;
  sats: number;
  speedKph: number | null;
}

export interface LinkState {
  source: TelemetrySource;
  /** True when a real feed (live or sim) is actively updating. */
  connected: boolean;
  /** Milliseconds since the last accepted frame, or null if never. */
  frameAgeMs: number | null;
  /** Measured frame rate (Hz), smoothed. */
  hz: number;
}

export interface Telemetry {
  // --- drive state machine ---
  driveState: DriveState;
  /** 0 = no fault; see FAULT_NAMES. */
  faultCode: number;
  gear: SpeedGear;
  reverse: boolean;
  /** Shift ladder in Park (neutral — throttle inhibited). */
  parked: boolean;

  // --- motion ---
  /** Hall-derived road speed (mph). Bridge computes from hall_hz. */
  speedMph: number;
  throttlePct: number;
  brakePct: number;
  steerSetpointDeg: number;
  steerMeasuredDeg: number;

  // --- status flags (from status_flags bitfield) ---
  wheelConnected: boolean;
  steerLinkOk: boolean;
  steerCalibrated: boolean;
  escLinkOk: boolean;
  contactor: ContactorPhase;
  brakeActive: boolean;
  benchMode: boolean;

  // --- power (null until ESC/BMS telemetry lands, Phase 5) ---
  battVolts: number | null;
  battAmps: number | null;
  escRpm: number | null;
  controllerTempC: number | null;
  motorTempC: number | null;

  // --- meta ---
  uptimeMs: number;
  seq: number;

  // --- merged GPS ---
  gps: GpsFix;

  // --- link health (dashboard-side, never from the wire) ---
  link: LinkState;
}

/** Full-scale of the speed dial, in mph. */
export const SPEED_MAX_MPH = 40;

/** Full-scale (± lock) of the steering dial, in degrees. */
export const STEER_MAX_DEG = 45;

/** Fault code → human name (uart-protocol.md §3). */
export const FAULT_NAMES: Record<number, string> = {
  1: 'Wheel lost',
  2: 'Steer timeout',
  3: 'Steer fault',
  4: 'Pedal implausible',
  5: 'DAC error',
  6: 'Armed timeout',
  7: 'Internal watchdog',
  8: 'Contactor fault',
};

export function faultName(code: number): string {
  return FAULT_NAMES[code] ?? `Fault ${code}`;
}

/**
 * The telemetry state used when no source is connected. Everything reads as
 * unknown/zero — deliberately not "plausible" values, so a disconnected dash
 * is obviously disconnected.
 */
export const DISCONNECTED: Telemetry = {
  driveState: 'SAFE',
  faultCode: 0,
  gear: 'LOW',
  reverse: false,
  parked: true,
  speedMph: 0,
  throttlePct: 0,
  brakePct: 0,
  steerSetpointDeg: 0,
  steerMeasuredDeg: 0,
  wheelConnected: false,
  steerLinkOk: false,
  steerCalibrated: false,
  escLinkOk: false,
  contactor: 'OPEN',
  brakeActive: false,
  benchMode: false,
  battVolts: null,
  battAmps: null,
  escRpm: null,
  controllerTempC: null,
  motorTempC: null,
  uptimeMs: 0,
  seq: 0,
  gps: { fix: false, lat: null, lon: null, headingDeg: null, sats: 0, speedKph: null },
  link: { source: 'none', connected: false, frameAgeMs: null, hz: 0 },
};

/** Which gear-stack cell is active, derived from the shift ladder. */
export function gearSelector(t: Telemetry): GearSelector {
  if (t.reverse) return 'R';
  if (t.parked) return 'P';
  if (t.driveState === 'DRIVE') {
    return t.gear === 'LOW' ? 'L' : t.gear === 'MED' ? 'M' : 'H';
  }
  return 'P';
}
