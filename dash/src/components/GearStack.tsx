import React from 'react';
import { motion } from 'framer-motion';
import type { GearSelector } from '../telemetry/types';

interface Cell {
  id: GearSelector;
  label: string;
  word: string;
  color: string; // active text/accent color
}

// Top-to-bottom: Park, Low, Middle, High, Reverse.
const CELLS: Cell[] = [
  { id: 'P', label: 'P', word: 'Park', color: '#d8cfbf' },
  { id: 'L', label: 'L', word: 'Low', color: '#34d399' },
  { id: 'M', label: 'M', word: 'Mid', color: '#22d3ee' },
  { id: 'H', label: 'H', word: 'High', color: '#60a5fa' },
  { id: 'R', label: 'R', word: 'Rev', color: '#fbbf24' },
];

export const GearStack: React.FC<{ active: GearSelector }> = ({ active }) => (
  <div className="flex flex-col items-stretch gap-[3px]">
    {CELLS.map((c) => {
      const isActive = c.id === active;
      return (
        <div
          key={c.id}
          className="relative flex items-center justify-center gap-1.5 rounded-md px-2 py-[2px]"
          style={{ opacity: isActive ? 1 : 0.26 }}
        >
          {isActive ? (
            <motion.span
              layoutId="gear-active"
              className="absolute inset-0 rounded-md"
              style={{ backgroundColor: `${c.color}1f`, boxShadow: `inset 0 0 0 1.5px ${c.color}` }}
              transition={{ type: 'spring', stiffness: 400, damping: 32 }}
            />
          ) : null}
          <span
            className="relative z-10 w-4 text-center text-lg font-black leading-none"
            style={{ color: isActive ? c.color : '#9a9284' }}
          >
            {c.label}
          </span>
          <span
            className="relative z-10 w-9 text-left text-[10px] font-semibold uppercase tracking-[0.12em] leading-none"
            style={{ color: isActive ? c.color : '#6b6459' }}
          >
            {c.word}
          </span>
        </div>
      );
    })}
  </div>
);
