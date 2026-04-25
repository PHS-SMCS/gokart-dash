import React from 'react';
import { motion } from 'framer-motion';
import { Battery, Lightbulb, Plug, Power, Thermometer } from 'lucide-react';
import { SPRING_SNAP } from '../constants/motion';
import type { Telemetry } from '../hooks/useTelemetry';

interface DriveViewProps {
  telemetry: Telemetry;
}

export const DriveView: React.FC<DriveViewProps> = ({ telemetry }) => {
  const rpmPct = clamp01(telemetry.rpm / telemetry.rpmMax);
  const redlinePct = clamp01(telemetry.rpmRedline / telemetry.rpmMax);
  const motorTempPct = clamp01(telemetry.motorTempF / telemetry.motorTempMaxF);

  return (
    <div className="flex h-full w-full flex-col gap-2 p-3">
      <RpmBar value={rpmPct} redlineAt={redlinePct} rpm={telemetry.rpm} max={telemetry.rpmMax} />

      <div className="flex flex-1 items-stretch gap-2">
        <GearModePanel gear={telemetry.gear} mode={telemetry.mode} />
        <SpeedPanel speed={telemetry.speedMph} />
        <PedalPanel throttle={telemetry.throttlePct} brake={telemetry.brakePct} />
      </div>

      <StatusRow
        batteryPct={telemetry.batteryPct}
        motorTempF={telemetry.motorTempF}
        motorTempPct={motorTempPct}
        rangeMi={telemetry.rangeMi}
        headlights={telemetry.headlights}
        contactor={telemetry.contactor}
      />
    </div>
  );
};

const RpmBar: React.FC<{ value: number; redlineAt: number; rpm: number; max: number }> = ({
  value,
  redlineAt,
  rpm,
  max,
}) => {
  const inRedline = value >= redlineAt;

  return (
    <div className="relative h-9 w-full overflow-hidden rounded-md border border-white/5 bg-[#15110d]">
      <div
        className="absolute inset-y-0 left-0 bg-red-500/20"
        style={{ left: `${redlineAt * 100}%`, right: 0 }}
      />
      <motion.div
        className={`absolute inset-y-0 left-0 ${inRedline ? 'bg-red-500' : 'bg-[#e6ddd0]'}`}
        animate={{ width: `${value * 100}%` }}
        transition={SPRING_SNAP}
      />
      <div className="absolute inset-0 flex items-center justify-between px-3 text-[10px] font-semibold uppercase tracking-[0.2em] text-black/80 mix-blend-screen">
        <span className="text-gray-400">RPM</span>
        <span className="text-white">
          {rpm.toLocaleString()} <span className="text-gray-500">/ {max.toLocaleString()}</span>
        </span>
      </div>
    </div>
  );
};

const GearModePanel: React.FC<{ gear: string; mode: string }> = ({ gear, mode }) => (
  <div className="flex w-32 flex-col items-center justify-center rounded-lg border border-white/5 bg-black/40">
    <p className="text-[10px] font-semibold uppercase tracking-[0.22em] text-gray-500">Gear</p>
    <p className="text-7xl font-black leading-none tracking-tighter text-[#e6ddd0]">{gear}</p>
    <p className="mt-3 text-[10px] font-semibold uppercase tracking-[0.22em] text-gray-500">Mode</p>
    <p className="mt-0.5 text-xs font-bold tracking-wider text-white">{mode}</p>
  </div>
);

const SpeedPanel: React.FC<{ speed: number }> = ({ speed }) => (
  <div className="flex flex-1 flex-col items-center justify-center rounded-lg border border-white/5 bg-black/40">
    <motion.p
      key={speed}
      initial={{ opacity: 0.6 }}
      animate={{ opacity: 1 }}
      transition={{ duration: 0.12 }}
      className="text-[180px] font-black leading-none tracking-[-0.06em] text-white tabular-nums"
    >
      {speed}
    </motion.p>
    <p className="mt-1 text-xs font-semibold uppercase tracking-[0.32em] text-gray-400">MPH</p>
  </div>
);

const PedalPanel: React.FC<{ throttle: number; brake: number }> = ({ throttle, brake }) => (
  <div className="flex w-32 gap-1.5 rounded-lg border border-white/5 bg-black/40 p-2">
    <PedalBar label="THR" pct={throttle} fill="bg-emerald-500" />
    <PedalBar label="BRK" pct={brake} fill="bg-red-500" />
  </div>
);

const PedalBar: React.FC<{ label: string; pct: number; fill: string }> = ({ label, pct, fill }) => (
  <div className="flex flex-1 flex-col items-center gap-1">
    <div className="relative flex-1 w-full overflow-hidden rounded-md bg-[#15110d]">
      <motion.div
        className={`absolute inset-x-0 bottom-0 ${fill}`}
        animate={{ height: `${clamp01(pct / 100) * 100}%` }}
        transition={SPRING_SNAP}
      />
    </div>
    <p className="text-[10px] font-semibold uppercase tracking-[0.18em] text-gray-400">{label}</p>
    <p className="text-xs font-bold tabular-nums text-white">{pct}</p>
  </div>
);

const StatusRow: React.FC<{
  batteryPct: number;
  motorTempF: number;
  motorTempPct: number;
  rangeMi: number;
  headlights: boolean;
  contactor: boolean;
}> = ({ batteryPct, motorTempF, motorTempPct, rangeMi, headlights, contactor }) => {
  const tempColor =
    motorTempPct > 0.85 ? 'text-red-400' : motorTempPct > 0.7 ? 'text-amber-400' : 'text-[#e6ddd0]';

  return (
    <div className="grid h-14 shrink-0 grid-cols-5 gap-2">
      <Pill icon={Battery} label="Batt" value={`${batteryPct}%`} />
      <Pill icon={Plug} label="Range" value={`${rangeMi.toFixed(1)} mi`} />
      <Pill icon={Thermometer} label="Motor" value={`${motorTempF}°F`} valueClassName={tempColor} />
      <Pill
        icon={Lightbulb}
        label="Lights"
        value={headlights ? 'On' : 'Off'}
        valueClassName={headlights ? 'text-amber-300' : 'text-gray-500'}
      />
      <Pill
        icon={Power}
        label="Contactor"
        value={contactor ? 'Closed' : 'Open'}
        valueClassName={contactor ? 'text-emerald-400' : 'text-gray-500'}
      />
    </div>
  );
};

const Pill: React.FC<{
  icon: React.ComponentType<{ size?: number; className?: string }>;
  label: string;
  value: string;
  valueClassName?: string;
}> = ({ icon: Icon, label, value, valueClassName = 'text-white' }) => (
  <div className="flex flex-col items-start justify-center rounded-md border border-white/5 bg-black/40 px-2.5 py-1">
    <div className="flex items-center gap-1 text-[9px] font-semibold uppercase tracking-[0.2em] text-gray-500">
      <Icon size={10} />
      {label}
    </div>
    <p className={`text-base font-bold leading-tight tabular-nums ${valueClassName}`}>{value}</p>
  </div>
);

function clamp01(n: number) {
  return Math.min(1, Math.max(0, n));
}
