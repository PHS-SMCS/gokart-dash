// Operator-driven simulator. These are the *starting positions of the admin
// controls* on the System tab — they only ever reach the UI when the operator
// explicitly selects the "Simulated" source. Nothing here is shown as live
// data by default.

import type {
  ContactorPhase,
  DriveState,
  SpeedGear,
  Telemetry,
  LinkState,
} from './types';

export interface SimControls {
  driveState: DriveState;
  faultCode: number;
  gear: SpeedGear;
  reverse: boolean;
  speedMph: number;
  throttlePct: number;
  brakePct: number;
  steerSetpointDeg: number;
  steerMeasuredDeg: number;
  wheelConnected: boolean;
  steerLinkOk: boolean;
  steerCalibrated: boolean;
  escLinkOk: boolean;
  contactor: ContactorPhase;
  brakeActive: boolean;
  benchMode: boolean;
  battVolts: number | null;
  battAmps: number | null;
  motorTempC: number | null;
  controllerTempC: number | null;
  escRpm: number | null;
  gpsFix: boolean;
  gpsLat: number;
  gpsLon: number;
  gpsHeadingDeg: number;
  gpsSats: number;
  gpsSpeedKph: number | null;
}

export const SIM_DEFAULTS: SimControls = {
  driveState: 'SAFE',
  faultCode: 0,
  gear: 'LOW',
  reverse: false,
  speedMph: 0,
  throttlePct: 0,
  brakePct: 0,
  steerSetpointDeg: 0,
  steerMeasuredDeg: 0,
  wheelConnected: true,
  steerLinkOk: true,
  steerCalibrated: true,
  escLinkOk: false,
  contactor: 'OPEN',
  brakeActive: false,
  benchMode: true,
  battVolts: 76.0,
  battAmps: 0,
  motorTempC: 32,
  controllerTempC: 30,
  escRpm: 0,
  gpsFix: true,
  gpsLat: 38.8977,
  gpsLon: -77.0365,
  gpsHeadingDeg: 0,
  gpsSats: 9,
  gpsSpeedKph: 0,
};

export function buildFromSim(c: SimControls, link: LinkState): Telemetry {
  return {
    driveState: c.driveState,
    faultCode: c.faultCode,
    gear: c.gear,
    reverse: c.reverse,
    speedMph: c.speedMph,
    throttlePct: c.throttlePct,
    brakePct: c.brakePct,
    steerSetpointDeg: c.steerSetpointDeg,
    steerMeasuredDeg: c.steerMeasuredDeg,
    wheelConnected: c.wheelConnected,
    steerLinkOk: c.steerLinkOk,
    steerCalibrated: c.steerCalibrated,
    escLinkOk: c.escLinkOk,
    contactor: c.contactor,
    brakeActive: c.brakeActive || c.brakePct > 3,
    benchMode: c.benchMode,
    battVolts: c.battVolts,
    battAmps: c.battAmps,
    escRpm: c.escRpm,
    controllerTempC: c.controllerTempC,
    motorTempC: c.motorTempC,
    uptimeMs: Math.round(performance.now()),
    seq: 0,
    gps: {
      fix: c.gpsFix,
      lat: c.gpsFix ? c.gpsLat : null,
      lon: c.gpsFix ? c.gpsLon : null,
      headingDeg: c.gpsHeadingDeg,
      sats: c.gpsSats,
      speedKph: c.gpsSpeedKph,
    },
    link,
  };
}
