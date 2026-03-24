import React from 'react';
import { motion } from 'framer-motion';
import { type LucideIcon } from 'lucide-react';

interface AppIconProps {
  id: string;
  name: string;
  icon: LucideIcon;
  color: string;
  onClick?: () => void;
}

export const AppIcon: React.FC<AppIconProps> = ({ name, icon: Icon, color, onClick }) => {
  return (
    <motion.div
      whileTap={{ scale: 0.9 }}
      className="flex flex-col items-center justify-center gap-3 w-32 shrink-0 snap-center cursor-pointer touch-manipulation user-select-none"
      onClick={onClick}
    >
      <div
        className={`w-28 h-28 sm:w-32 sm:h-32 rounded-3xl bg-gradient-to-br ${color} flex items-center justify-center shadow-lg shadow-black/50 border border-white/10 backdrop-blur-md`}
      >
        <Icon size={48} className="text-white drop-shadow-md" />
      </div>
      <span className="text-white text-base sm:text-lg font-medium tracking-wide drop-shadow-md text-center leading-tight">
        {name}
      </span>
    </motion.div>
  );
};
