import React from 'react';
import { motion } from 'framer-motion';

const SPRING = { type: 'spring', stiffness: 300, damping: 30 } as const;

function clamp01(n: number) {
  return Math.min(1, Math.max(0, n));
}

const Bar: React.FC<{ label: string; pct: number; fill: string; glow: string }> = ({
  label,
  pct,
  fill,
  glow,
}) => (
  <div className="flex h-full w-full flex-col items-center gap-1">
    <span className="text-[10px] font-bold tabular-nums text-white">{Math.round(pct)}</span>
    <div className="relative w-full flex-1 overflow-hidden rounded-full border border-white/5 bg-[#15110d]">
      <motion.div
        className={`absolute inset-x-0 bottom-0 ${fill}`}
        style={{ boxShadow: `0 0 12px ${glow}` }}
        animate={{ height: `${clamp01(pct / 100) * 100}%` }}
        transition={SPRING}
      />
    </div>
    <span className="text-[9px] font-semibold uppercase tracking-[0.14em] text-gray-500">{label}</span>
  </div>
);

export const PedalBars: React.FC<{ throttle: number; brake: number }> = ({ throttle, brake }) => (
  <div className="flex h-full gap-1.5">
    <Bar label="Thr" pct={throttle} fill="bg-emerald-500" glow="rgba(16,185,129,0.5)" />
    <Bar label="Brk" pct={brake} fill="bg-red-500" glow="rgba(239,68,68,0.5)" />
  </div>
);
