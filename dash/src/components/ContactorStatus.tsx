import React from 'react';
import { motion } from 'framer-motion';
import { Power, Zap, AlertTriangle, Unplug } from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import type { ContactorPhase } from '../telemetry/types';

const CONFIG: Record<
  ContactorPhase,
  { icon: LucideIcon; label: string; text: string; ring: string; bg: string; pulse: boolean }
> = {
  OPEN: { icon: Unplug, label: 'Open', text: 'text-gray-400', ring: 'border-white/10', bg: 'bg-black/40', pulse: false },
  PRECHARGE: { icon: Zap, label: 'Precharge', text: 'text-amber-300', ring: 'border-amber-400/40', bg: 'bg-amber-400/10', pulse: true },
  CLOSED: { icon: Power, label: 'Closed', text: 'text-emerald-300', ring: 'border-emerald-400/40', bg: 'bg-emerald-400/10', pulse: false },
  FAULT: { icon: AlertTriangle, label: 'Fault', text: 'text-red-300', ring: 'border-red-500/50', bg: 'bg-red-500/10', pulse: true },
};

export const ContactorStatus: React.FC<{ phase: ContactorPhase }> = ({ phase }) => {
  const c = CONFIG[phase];
  const Icon = c.icon;
  return (
    <div className={`flex items-center gap-2 rounded-lg border ${c.ring} ${c.bg} px-2.5 py-1.5`}>
      <motion.span
        animate={c.pulse ? { opacity: [1, 0.35, 1] } : { opacity: 1 }}
        transition={c.pulse ? { duration: 1, repeat: Infinity } : { duration: 0.2 }}
        className={c.text}
      >
        <Icon size={16} />
      </motion.span>
      <div className="min-w-0 leading-none">
        <p className="text-[8px] font-semibold uppercase tracking-[0.2em] text-gray-500">Contactor</p>
        <p className={`text-xs font-bold ${c.text}`}>{c.label}</p>
      </div>
    </div>
  );
};
