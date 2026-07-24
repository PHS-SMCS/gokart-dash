import React from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { AlertTriangle, CheckCircle2, Info, XOctagon } from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import { useNotifications, type NoticeLevel } from './context';

const STYLES: Record<NoticeLevel, { icon: LucideIcon; ring: string; accent: string }> = {
  info: { icon: Info, ring: 'border-sky-400/30', accent: 'text-sky-300' },
  success: { icon: CheckCircle2, ring: 'border-emerald-400/30', accent: 'text-emerald-300' },
  warning: { icon: AlertTriangle, ring: 'border-amber-400/40', accent: 'text-amber-300' },
  danger: { icon: XOctagon, ring: 'border-red-500/50', accent: 'text-red-300' },
};

export const NotificationHost: React.FC = () => {
  const { notices, dismiss } = useNotifications();

  return (
    <div className="pointer-events-none fixed right-3 top-3 z-50 flex w-[min(22rem,70vw)] flex-col gap-2">
      <AnimatePresence initial={false}>
        {notices.map((n) => {
          const s = STYLES[n.level];
          const Icon = s.icon;
          return (
            <motion.button
              key={n.id}
              type="button"
              layout
              initial={{ opacity: 0, x: 40, scale: 0.96 }}
              animate={{ opacity: 1, x: 0, scale: 1 }}
              exit={{ opacity: 0, x: 40, scale: 0.96 }}
              transition={{ type: 'spring', stiffness: 320, damping: 30 }}
              onClick={() => dismiss(n.id)}
              className={`pointer-events-auto flex w-full items-start gap-2.5 rounded-xl border ${s.ring} bg-[#12100c]/95 px-3 py-2.5 text-left shadow-[0_8px_24px_rgba(0,0,0,0.5)] backdrop-blur`}
            >
              <Icon size={18} className={`mt-0.5 shrink-0 ${s.accent}`} />
              <div className="min-w-0 flex-1">
                <p className="text-sm font-bold leading-tight text-white">{n.title}</p>
                {n.message ? (
                  <p className="mt-0.5 text-xs leading-snug text-gray-400">{n.message}</p>
                ) : null}
              </div>
            </motion.button>
          );
        })}
      </AnimatePresence>
    </div>
  );
};
