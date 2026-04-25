import React from 'react';
import type { LucideIcon } from 'lucide-react';

interface PlaceholderProps {
  label: string;
  icon: LucideIcon;
}

export const Placeholder: React.FC<PlaceholderProps> = ({ label, icon: Icon }) => (
  <div className="flex h-full w-full flex-col items-center justify-center gap-3 text-gray-500">
    <Icon size={64} strokeWidth={1.2} />
    <p className="text-sm font-semibold uppercase tracking-[0.32em]">{label}</p>
    <p className="text-xs text-gray-600">Coming soon</p>
  </div>
);
