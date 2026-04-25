import React from 'react';
import { motion } from 'framer-motion';
import { VIEWS, type ViewId } from '../constants/views';
import { SPRING_SNAP } from '../constants/motion';

interface BottomDockProps {
  active: ViewId;
  onSelect: (id: ViewId) => void;
}

export const BottomDock: React.FC<BottomDockProps> = ({ active, onSelect }) => {
  return (
    <nav className="flex h-14 shrink-0 items-stretch border-t border-white/5 bg-black/70 backdrop-blur-xl">
      {VIEWS.map((view) => {
        const Icon = view.icon;
        const isActive = active === view.id;

        return (
          <motion.button
            key={view.id}
            type="button"
            whileTap={{ scale: 0.92 }}
            transition={SPRING_SNAP}
            onClick={() => onSelect(view.id)}
            className={`relative flex flex-1 touch-manipulation select-none flex-col items-center justify-center gap-0.5 ${
              isActive ? 'text-white' : 'text-gray-500'
            }`}
          >
            <Icon size={20} strokeWidth={isActive ? 2.4 : 1.8} />
            <span className="text-[10px] font-semibold uppercase tracking-[0.16em]">{view.label}</span>
            {isActive ? (
              <motion.span
                layoutId="dock-active"
                className="absolute inset-x-3 top-0 h-0.5 rounded-full bg-[#e6ddd0]"
                transition={SPRING_SNAP}
              />
            ) : null}
          </motion.button>
        );
      })}
    </nav>
  );
};
