import React, { useEffect, useState } from 'react';
import { BatteryMedium, Satellite, Radio, WifiOff } from 'lucide-react';
import type { Telemetry } from '../telemetry/types';

export const StatusBar: React.FC<{ telemetry: Telemetry }> = ({ telemetry: t }) => {
  const [now, setNow] = useState(new Date());
  useEffect(() => {
    const id = setInterval(() => setNow(new Date()), 15_000);
    return () => clearInterval(id);
  }, []);

  const time = now.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  const connected = t.link.connected;

  return (
    <header className="flex h-9 shrink-0 items-center justify-between border-b border-white/5 bg-black/60 px-3 text-[11px] font-semibold uppercase tracking-[0.16em] text-gray-300">
      <div className="flex items-center gap-3">
        <span className="text-white">{time}</span>
        <span className="text-gray-600">·</span>
        <span className={connected ? 'flex items-center gap-1 text-emerald-400' : 'flex items-center gap-1 text-gray-500'}>
          {connected ? <Radio size={12} /> : <WifiOff size={12} />}
          {connected ? (t.link.source === 'sim' ? 'Sim' : 'Live') : 'No link'}
        </span>
        {t.benchMode ? <span className="rounded bg-white/10 px-1.5 py-0.5 text-[9px] text-gray-300">Bench</span> : null}
      </div>

      <div className="flex items-center gap-3">
        <span className="flex items-center gap-1 text-gray-300">
          <Satellite size={13} className={t.gps.fix ? 'text-emerald-400' : 'text-gray-500'} />
          {t.gps.sats}
        </span>
        <span className="flex items-center gap-1 text-gray-300">
          <BatteryMedium size={14} />
          {t.battVolts != null ? `${t.battVolts.toFixed(1)}V` : '—'}
        </span>
      </div>
    </header>
  );
};
