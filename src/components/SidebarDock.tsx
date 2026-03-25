import React, { useEffect, useState } from 'react';
import { CircleGauge, Grid, Home } from 'lucide-react';
import { motion } from 'framer-motion';
import { SPRING_SNAP } from '../constants/motion';

interface SidebarDockProps {
  onHomePress?: () => void;
  onAppDrawerPress?: () => void;
}

export const SidebarDock: React.FC<SidebarDockProps> = ({
  onHomePress,
  onAppDrawerPress,
}) => {
  const [time, setTime] = useState<Date>(new Date());

  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 60000);
    return () => clearInterval(timer);
  }, []);

  const formatTime = (date: Date) => {
    return date.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  };

  return (
    <aside className="relative h-full w-[14%] min-w-[145px] shrink-0 rounded-3xl border border-white/10 bg-black/40 backdrop-blur-2xl">
      <div className="flex h-full flex-col items-center justify-between p-4">
        <div className="flex w-full flex-col items-center gap-5">
          <div className="rounded-full border border-white/10 bg-black/45 px-4 py-2 backdrop-blur-2xl">
            <p className="text-base font-extrabold tracking-tight text-white">{formatTime(time)}</p>
          </div>

          <div className="flex w-full flex-col items-center rounded-3xl border border-white/10 bg-black/45 py-4 backdrop-blur-2xl">
            <CircleGauge size={26} className="text-[#e6ddd0]" />
            <p className="mt-2 text-3xl font-extrabold leading-none tracking-tighter text-white">0</p>
            <p className="mt-1 text-[11px] font-medium uppercase tracking-[0.22em] text-gray-400">mph</p>
          </div>
        </div>

        <div className="flex w-full flex-col items-center gap-4">
          <motion.button
            type="button"
            whileTap={{ scale: 0.92 }}
            transition={SPRING_SNAP}
            className="flex h-16 w-16 touch-manipulation select-none items-center justify-center rounded-3xl border border-white/10 bg-black/45 text-white backdrop-blur-2xl"
            onClick={onHomePress}
          >
            <Home size={24} />
          </motion.button>

          <motion.button
            type="button"
            whileTap={{ scale: 0.92 }}
            transition={SPRING_SNAP}
            className="flex h-16 w-16 touch-manipulation select-none items-center justify-center rounded-3xl border border-white/10 bg-black/45 text-white backdrop-blur-2xl"
            onClick={onAppDrawerPress}
          >
            <Grid size={24} />
          </motion.button>
        </div>
      </div>
    </aside>
  );
};
