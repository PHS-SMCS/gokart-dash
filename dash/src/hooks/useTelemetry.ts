import { useEffect, useState } from 'react';

export type Gear = 'P' | 'R' | 'N' | 'D';
export type DriveMode = 'SAFE' | 'TURBO' | '2FST2BVR';

export interface Telemetry {
  speedMph: number;
  rpm: number;
  rpmMax: number;
  rpmRedline: number;
  throttlePct: number;
  brakePct: number;
  gear: Gear;
  mode: DriveMode;
  batteryPct: number;
  motorTempF: number;
  motorTempMaxF: number;
  rangeMi: number;
  gpsSats: number;
  gpsFix: boolean;
  headlights: boolean;
  contactor: boolean;
  armed: boolean;
}

const SEED: Telemetry = {
  speedMph: 0,
  rpm: 0,
  rpmMax: 7000,
  rpmRedline: 6200,
  throttlePct: 0,
  brakePct: 0,
  gear: 'N',
  mode: 'SAFE',
  batteryPct: 84,
  motorTempF: 152,
  motorTempMaxF: 240,
  rangeMi: 18.4,
  gpsSats: 0,
  gpsFix: false,
  headlights: false,
  contactor: false,
  armed: false,
};

// Stand-in for the real WebSocket bridge. Produces gentle motion so the UI
// doesn't look frozen during bring-up. Replace with a ws:// subscription
// once gokart-telemetry.service is in place.
export function useTelemetry(): Telemetry {
  const [data, setData] = useState<Telemetry>(SEED);

  useEffect(() => {
    let t = 0;
    const id = setInterval(() => {
      t += 0.1;
      const throttle = Math.max(0, 0.5 + 0.5 * Math.sin(t * 0.6));
      const speed = Math.round(throttle * 28);
      const rpm = Math.round(throttle * 4200 + 800);
      setData((prev) => ({
        ...prev,
        speedMph: speed,
        rpm,
        throttlePct: Math.round(throttle * 100),
        brakePct: 0,
        gear: speed > 0 ? 'D' : 'N',
        motorTempF: 150 + Math.round(throttle * 30),
        gpsSats: 9,
        gpsFix: true,
        contactor: true,
        armed: true,
      }));
    }, 250);
    return () => clearInterval(id);
  }, []);

  return data;
}
