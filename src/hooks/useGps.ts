import { useEffect, useState } from 'react';

export interface GpsSnapshot {
  fix: boolean;
  lat: number | null;
  lon: number | null;
  altM: number | null;
  sats: number;
  hdop: number | null;
  speedKph: number | null;
  headingDeg: number | null;
  utc: string | null;
  ageMs: number | null;
  streamOk: boolean;
  bridgeOk: boolean;
  error: string | null;
}

const BRIDGE_BASE = `http://${window.location.hostname}:5174`;

const SEED: GpsSnapshot = {
  fix: false,
  lat: null,
  lon: null,
  altM: null,
  sats: 0,
  hdop: null,
  speedKph: null,
  headingDeg: null,
  utc: null,
  ageMs: null,
  streamOk: false,
  bridgeOk: false,
  error: null,
};

export function useGps(intervalMs = 1000): GpsSnapshot {
  const [snap, setSnap] = useState<GpsSnapshot>(SEED);

  useEffect(() => {
    let cancelled = false;
    let timer: number | null = null;

    const tick = async () => {
      try {
        const res = await fetch(`${BRIDGE_BASE}/api/gps`, { cache: 'no-store' });
        if (!res.ok) throw new Error(`bridge ${res.status}`);
        const body = await res.json();
        if (cancelled) return;
        if (!body.ok) {
          setSnap((s) => ({ ...s, bridgeOk: false, error: body.error ?? 'bridge error' }));
        } else {
          const g = body.gps ?? {};
          setSnap({
            fix: !!g.fix,
            lat: typeof g.lat === 'number' ? g.lat : null,
            lon: typeof g.lon === 'number' ? g.lon : null,
            altM: typeof g.alt_m === 'number' ? g.alt_m : null,
            sats: typeof g.sats === 'number' ? g.sats : 0,
            hdop: typeof g.hdop === 'number' ? g.hdop : null,
            speedKph: typeof g.speed_kph === 'number' ? g.speed_kph : null,
            headingDeg: typeof g.heading_deg === 'number' ? g.heading_deg : null,
            utc: typeof g.utc === 'string' ? g.utc : null,
            ageMs: typeof g.age_ms === 'number' ? g.age_ms : null,
            streamOk: !!g.stream_ok,
            bridgeOk: true,
            error: g.error ?? null,
          });
        }
      } catch (err) {
        if (!cancelled) {
          setSnap((s) => ({ ...s, bridgeOk: false, error: (err as Error).message }));
        }
      } finally {
        if (!cancelled) {
          timer = window.setTimeout(tick, intervalMs);
        }
      }
    };

    tick();
    return () => {
      cancelled = true;
      if (timer !== null) window.clearTimeout(timer);
    };
  }, [intervalMs]);

  return snap;
}
