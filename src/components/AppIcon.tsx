import React from 'react';
import { motion } from 'framer-motion';
import { type LucideIcon } from 'lucide-react';
import { SPRING_SNAP } from '../constants/motion';

interface AppIconProps {
  name: string;
  details?: string;
  icon: LucideIcon;
  color: string;
  onClick?: () => void;
}

export const AppIcon: React.FC<AppIconProps> = ({
  name,
  details,
  icon: Icon,
  color,
  onClick,
}) => {
  return (
    <motion.button
      type="button"
      whileTap={{ scale: 0.94 }}
      transition={SPRING_SNAP}
      className="flex w-44 touch-manipulation select-none flex-col items-center justify-center gap-3"
      onClick={onClick}
    >
      <div className="relative flex h-36 w-36 items-center justify-center overflow-hidden rounded-3xl border border-white/10 bg-black/40 backdrop-blur-2xl lg:h-40 lg:w-40">
        <div className={`absolute inset-0 bg-gradient-to-br ${color} opacity-60`} />
        <div className="absolute inset-0 bg-[radial-gradient(70%_65%_at_25%_20%,rgba(255,255,255,0.35),transparent_65%)]" />
        <Icon size={60} className="relative z-10 text-white" />
      </div>

      <span className="text-center text-lg font-semibold leading-tight tracking-tight text-white lg:text-xl">
        {name}
      </span>

      {details ? (
        <span className="text-center text-xs font-medium tracking-wide text-gray-400 lg:text-sm">
          {details}
        </span>
      ) : null}
    </motion.button>
  );
};
