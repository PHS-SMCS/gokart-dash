import React from 'react';
import { APPS } from '../constants/apps';
import { AppIcon } from './AppIcon';

interface AppGridProps {
  onLaunchApp: (appId: string) => void;
}

export const AppGrid: React.FC<AppGridProps> = ({ onLaunchApp }) => {
  return (
    <div className="relative flex h-full w-full overflow-hidden bg-[#0a0a0a]">
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(70%_55%_at_50%_108%,rgba(120,140,170,0.18),transparent_70%)]" />

      <div className="relative z-10 flex h-full w-full flex-col px-8 py-8">
        <header className="rounded-3xl border border-white/10 bg-black/35 px-5 py-3 backdrop-blur-2xl">
          <p className="text-xs font-medium uppercase tracking-[0.24em] text-gray-400">Launcher</p>
          <p className="mt-1 text-2xl font-extrabold tracking-tight text-white">Go-Kart Apps</p>
        </header>

        <div className="mt-8 grid flex-1 grid-cols-2 place-items-center gap-x-8 gap-y-8 xl:grid-cols-3">
          {APPS.map((app) => (
            <AppIcon
              key={app.id}
              name={app.name}
              details={app.details}
              icon={app.icon}
              color={app.color}
              onClick={() => onLaunchApp(app.id)}
            />
          ))}
        </div>
      </div>
    </div>
  );
};
