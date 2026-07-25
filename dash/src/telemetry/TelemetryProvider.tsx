import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { Telemetry, TelemetrySource, LinkState } from './types';
import { DISCONNECTED } from './types';
import { decodeFrame, telemetryWsUrl } from './wire';
import { buildFromSim, SIM_DEFAULTS, type SimControls } from './sim';
import { TelemetryContext } from './context';

const SIM_TICK_MS = 50; // 20 Hz, matches the live telemetry rate
const STALE_MS = 600; // no live frame for this long ⇒ link considered down
const RECONNECT_MS = 1500;

export const TelemetryProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  // Default to the live bridge so the kiosk shows Teensy telemetry on boot;
  // the operator can switch to the simulator on the System tab. Shows "No link"
  // until the first frame arrives (or if the bridge is down).
  const [source, setSourceState] = useState<TelemetrySource>('live');
  const [telemetry, setTelemetry] = useState<Telemetry>({
    ...DISCONNECTED,
    link: { source: 'live', connected: false, frameAgeMs: null, hz: 0 },
  });
  const [sim, setSimState] = useState<SimControls>(SIM_DEFAULTS);

  // Refs so the long-lived socket/interval callbacks always see current values.
  const sourceRef = useRef(source);
  const simRef = useRef(sim);
  const telemetryRef = useRef(telemetry);
  const lastFrameAtRef = useRef<number | null>(null);
  const hzRef = useRef(0);

  // Keep refs in sync after commit (never write refs during render).
  useEffect(() => {
    sourceRef.current = source;
  }, [source]);
  useEffect(() => {
    simRef.current = sim;
  }, [sim]);
  useEffect(() => {
    telemetryRef.current = telemetry;
  }, [telemetry]);

  const setSim = useCallback((patch: Partial<SimControls>) => {
    setSimState((prev) => ({ ...prev, ...patch }));
  }, []);
  const resetSim = useCallback(() => setSimState(SIM_DEFAULTS), []);

  // Switching source resets frame bookkeeping and seeds the link state. Done
  // in the action (not an effect) so there's no cascading re-render.
  const setSource = useCallback((next: TelemetrySource) => {
    lastFrameAtRef.current = null;
    hzRef.current = 0;
    if (next === 'none') {
      setTelemetry(DISCONNECTED);
    } else if (next === 'live') {
      setTelemetry((prev) => ({
        ...prev,
        link: { source: 'live', connected: false, frameAgeMs: null, hz: 0 },
      }));
    }
    setSourceState(next);
  }, []);

  // --- live WebSocket source ---
  useEffect(() => {
    if (source !== 'live') return;

    let ws: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let closed = false;

    const connect = () => {
      if (closed) return;
      try {
        ws = new WebSocket(telemetryWsUrl());
      } catch {
        reconnectTimer = window.setTimeout(connect, RECONNECT_MS);
        return;
      }

      ws.onmessage = (ev) => {
        if (typeof ev.data !== 'string') return;
        const now = performance.now();
        const last = lastFrameAtRef.current;
        if (last != null) {
          const dt = now - last;
          if (dt > 0) hzRef.current = hzRef.current * 0.8 + (1000 / dt) * 0.2;
        }
        lastFrameAtRef.current = now;
        const next = decodeFrame(telemetryRef.current, ev.data);
        if (next) {
          next.link = { source: 'live', connected: true, frameAgeMs: 0, hz: hzRef.current };
          setTelemetry(next);
        }
      };

      ws.onclose = () => {
        if (closed) return;
        reconnectTimer = window.setTimeout(connect, RECONNECT_MS);
      };
      ws.onerror = () => ws?.close();
    };

    connect();

    return () => {
      closed = true;
      if (reconnectTimer !== null) window.clearTimeout(reconnectTimer);
      ws?.close();
    };
  }, [source]);

  // --- simulator source (20 Hz tick from the operator controls) ---
  useEffect(() => {
    if (source !== 'sim') return;
    const id = window.setInterval(() => {
      const link: LinkState = { source: 'sim', connected: true, frameAgeMs: 0, hz: 20 };
      setTelemetry(buildFromSim(simRef.current, link));
    }, SIM_TICK_MS);
    return () => window.clearInterval(id);
  }, [source]);

  // --- link-health watchdog: age out live frames, fall to disconnected ---
  useEffect(() => {
    const id = window.setInterval(() => {
      if (sourceRef.current !== 'live') return;
      const last = lastFrameAtRef.current;
      const age = last == null ? null : performance.now() - last;
      const connected = age != null && age < STALE_MS;
      setTelemetry((prev) => {
        if (prev.link.connected === connected && prev.link.frameAgeMs != null) {
          // still update the age reading
          return { ...prev, link: { ...prev.link, frameAgeMs: age, hz: connected ? hzRef.current : 0 } };
        }
        return {
          ...prev,
          link: { source: 'live', connected, frameAgeMs: age, hz: connected ? hzRef.current : 0 },
        };
      });
    }, 250);
    return () => window.clearInterval(id);
  }, []);

  const value = useMemo(
    () => ({ telemetry, source, setSource, sim, setSim, resetSim }),
    [telemetry, source, setSource, sim, setSim, resetSim]
  );

  return <TelemetryContext.Provider value={value}>{children}</TelemetryContext.Provider>;
};
