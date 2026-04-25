import React, { useEffect, useState } from 'react';
import { BatteryMedium, Satellite, ShieldCheck, ShieldAlert } from 'lucide-react';
import type { Telemetry } from '../hooks/useTelemetry';

interface StatusBarProps {
  telemetry: Telemetry;
}

export const StatusBar: React.FC<StatusBarProps> = ({ telemetry }) => {
  const [now, setNow] = useState(new Date());

  useEffect(() => {
    const id = setInterval(() => setNow(new Date()), 15_000);
    return () => clearInterval(id);
  }, []);

  const time = now.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  const ArmedIcon = telemetry.armed ? ShieldCheck : ShieldAlert;
  const armedColor = telemetry.armed ? 'text-emerald-400' : 'text-amber-400';

  return (
    <header className="flex h-9 shrink-0 items-center justify-between border-b border-white/5 bg-black/60 px-3 text-[11px] font-semibold uppercase tracking-[0.18em] text-gray-300">
      <div className="flex items-center gap-3">
        <span className="text-white">{time}</span>
        <span className="text-gray-500">·</span>
        <span className="text-gray-400">{telemetry.mode}</span>
      </div>

      <div className="flex items-center gap-3">
        <span className={`flex items-center gap-1 ${armedColor}`}>
          <ArmedIcon size={13} />
          {telemetry.armed ? 'Armed' : 'Safe'}
        </span>
        <span className="flex items-center gap-1 text-gray-300">
          <Satellite size={13} className={telemetry.gpsFix ? 'text-emerald-400' : 'text-gray-500'} />
          {telemetry.gpsSats}
        </span>
        <span className="flex items-center gap-1 text-gray-300">
          <BatteryMedium size={14} />
          {telemetry.batteryPct}%
        </span>
      </div>
    </header>
  );
};
