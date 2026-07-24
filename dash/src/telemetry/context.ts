import { createContext, useContext } from 'react';
import type { Telemetry, TelemetrySource } from './types';
import type { SimControls } from './sim';

export interface TelemetryContextValue {
  telemetry: Telemetry;
  source: TelemetrySource;
  setSource: (s: TelemetrySource) => void;
  sim: SimControls;
  setSim: (patch: Partial<SimControls>) => void;
  resetSim: () => void;
}

export const TelemetryContext = createContext<TelemetryContextValue | null>(null);

export function useTelemetry(): Telemetry {
  const ctx = useContext(TelemetryContext);
  if (!ctx) throw new Error('useTelemetry must be used within TelemetryProvider');
  return ctx.telemetry;
}

export function useTelemetryControl(): TelemetryContextValue {
  const ctx = useContext(TelemetryContext);
  if (!ctx) throw new Error('useTelemetryControl must be used within TelemetryProvider');
  return ctx;
}
