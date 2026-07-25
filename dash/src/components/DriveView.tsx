import React from 'react';
import { motion } from 'framer-motion';
import { WifiOff } from 'lucide-react';
import type { Telemetry, DriveState } from '../telemetry/types';
import { SPEED_MAX_MPH, STEER_MAX_DEG, faultName, gearSelector } from '../telemetry/types';
import { SpeedDial } from './SpeedDial';
import { SteeringDial } from './SteeringDial';
import { BatteryDial } from './BatteryDial';
import { GearStack } from './GearStack';
import { PedalBars } from './PedalBars';
import { MiniMap } from './MiniMap';
import { ContactorStatus } from './ContactorStatus';

const STATE_STYLE: Record<DriveState, { label: string; text: string; ring: string; pulse: boolean }> = {
  SAFE: { label: 'Safe', text: 'text-fuchsia-300', ring: 'border-fuchsia-400/40 bg-fuchsia-400/10', pulse: false },
  ARMED: { label: 'Armed', text: 'text-amber-300', ring: 'border-amber-400/40 bg-amber-400/10', pulse: false },
  DRIVE: { label: 'Drive', text: 'text-emerald-300', ring: 'border-emerald-400/40 bg-emerald-400/10', pulse: false },
  STOPPING: { label: 'Stopping', text: 'text-amber-300', ring: 'border-amber-400/50 bg-amber-400/10', pulse: true },
  FAULT: { label: 'Fault', text: 'text-red-300', ring: 'border-red-500/60 bg-red-500/15', pulse: true },
};

const DriveStateBadge: React.FC<{ state: DriveState; fault: number; bench: boolean }> = ({
  state,
  fault,
  bench,
}) => {
  const s = STATE_STYLE[state];
  return (
    <motion.div
      animate={s.pulse ? { opacity: [1, 0.5, 1] } : { opacity: 1 }}
      transition={s.pulse ? { duration: 1, repeat: Infinity } : { duration: 0.2 }}
      className={`flex items-center gap-2 rounded-lg border px-3 py-1.5 ${s.ring}`}
    >
      <span className={`text-sm font-black uppercase tracking-[0.14em] ${s.text}`}>{s.label}</span>
      {state === 'FAULT' && fault > 0 ? (
        <span className="text-[10px] font-semibold text-red-200/80">{faultName(fault)}</span>
      ) : null}
      {bench ? (
        <span className="rounded bg-white/10 px-1.5 py-0.5 text-[8px] font-bold uppercase tracking-[0.16em] text-gray-300">
          Bench
        </span>
      ) : null}
    </motion.div>
  );
};

export const DriveView: React.FC<{ telemetry: Telemetry }> = ({ telemetry: t }) => {
  const connected = t.link.connected;
  const selector = gearSelector(t);

  return (
    <div className="relative flex h-full w-full gap-3 p-3">
      {/* Left — speed dial with the gear stack in the middle */}
      <div className="relative flex flex-[1.25] flex-col">
        <div className="absolute left-0 top-0 z-10">
          <DriveStateBadge state={t.driveState} fault={t.faultCode} bench={t.benchMode} />
        </div>
        <div className="min-h-0 flex-1">
          <SpeedDial value={t.speedMph} max={SPEED_MAX_MPH} connected={connected}>
            <div className="flex flex-col items-center gap-1">
              <GearStack active={selector} />
              <div className="flex items-baseline gap-1">
                <span
                  className={`text-3xl font-black leading-none tabular-nums ${connected ? 'text-white' : 'text-gray-600'}`}
                >
                  {Math.round(t.speedMph)}
                </span>
                <span className="text-[9px] font-semibold uppercase tracking-[0.24em] text-gray-500">mph</span>
              </div>
              <span className="text-[10px] font-semibold tabular-nums tracking-wider text-gray-500">
                {t.escRpm != null ? `${Math.round(t.escRpm).toLocaleString()}` : '—'}
                <span className="ml-1 text-gray-600">RPM</span>
              </span>
            </div>
          </SpeedDial>
        </div>
        {/* Steering (left) and battery V+A (right) dials fill the space below
            the speedometer. Same footprint → same rendered size. */}
        <div className="flex h-[9rem] shrink-0 items-center justify-around px-2 pb-1">
          {/* Negated so the dial's left/right matches the driver's view: the
              firmware's measured/ setpoint angle sign is opposite to the dial's
              (negative = left) convention. */}
          <SteeringDial
            angleDeg={-t.steerMeasuredDeg}
            setpointDeg={-t.steerSetpointDeg}
            max={STEER_MAX_DEG}
            connected={connected}
          />
          <BatteryDial volts={t.battVolts} amps={t.battAmps} connected={connected} />
        </div>
      </div>

      {/* Right — minimap + pedal bars share the space; contactor below */}
      <div className="flex flex-1 flex-col gap-2">
        <div className="flex flex-1 gap-2">
          <div className="flex-1">
            <MiniMap gps={t.gps} />
          </div>
          <div className="w-[4.5rem] shrink-0">
            <PedalBars throttle={t.throttlePct} brake={t.brakePct} />
          </div>
        </div>
        <div className="flex shrink-0 items-stretch gap-2">
          <div className="flex-1">
            <ContactorStatus phase={t.contactor} />
          </div>
        </div>
      </div>

      {!connected ? (
        <div className="pointer-events-none absolute inset-x-0 bottom-2 flex justify-center">
          <div className="flex items-center gap-2 rounded-full border border-white/10 bg-black/70 px-3 py-1 text-[10px] font-semibold uppercase tracking-[0.2em] text-gray-400 backdrop-blur">
            <WifiOff size={12} className="text-amber-300" />
            No telemetry — enable a source on the System tab
          </div>
        </div>
      ) : null}
    </div>
  );
};
