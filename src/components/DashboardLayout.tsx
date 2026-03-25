import React, { useMemo, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ChevronLeft } from 'lucide-react';
import { APPS } from '../constants/apps';
import { SPRING_SNAP } from '../constants/motion';
import { SidebarDock } from './SidebarDock';
import { AppGrid } from './AppGrid';
import { TelemetryView } from './TelemetryView';

interface PlaceholderAppProps {
  appName: string;
}

const PlaceholderApp: React.FC<PlaceholderAppProps> = ({ appName }) => {
  return (
    <div className="relative flex h-full w-full items-center justify-center bg-[#0a0a0a]">
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(60%_40%_at_50%_105%,rgba(114,128,146,0.2),transparent_75%)]" />
      <div className="relative rounded-3xl border border-white/10 bg-black/40 px-12 py-10 text-center backdrop-blur-2xl">
        <p className="text-xs font-medium uppercase tracking-[0.24em] text-gray-400">{appName}</p>
        <p className="mt-3 text-3xl font-extrabold tracking-tight text-white">Coming Soon</p>
      </div>
    </div>
  );
};

export const DashboardLayout: React.FC = () => {
  const [activeAppId, setActiveAppId] = useState<string | null>(null);

  const activeApp = useMemo(
    () => APPS.find((app) => app.id === activeAppId) ?? null,
    [activeAppId]
  );

  const goToLauncher = () => {
    setActiveAppId(null);
  };

  return (
    <div className="relative h-screen w-screen overflow-hidden bg-[#060606] text-white">
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(120%_80%_at_50%_-20%,rgba(255,255,255,0.14),transparent_55%)]" />
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(75%_50%_at_50%_110%,rgba(78,104,145,0.2),transparent_75%)]" />

      <div className="relative z-10 flex h-full w-full gap-4 p-4">
        <SidebarDock onHomePress={goToLauncher} onAppDrawerPress={goToLauncher} />

        <main className="relative flex-1 overflow-hidden rounded-3xl border border-white/10 bg-black/40 backdrop-blur-2xl">
          {activeApp ? (
            <motion.button
              type="button"
              whileTap={{ scale: 0.95 }}
              transition={SPRING_SNAP}
              onClick={goToLauncher}
              className="absolute left-4 top-4 z-20 flex touch-manipulation select-none items-center gap-2 rounded-full border border-white/10 bg-black/40 px-4 py-2 text-xs font-medium uppercase tracking-[0.18em] text-gray-200 backdrop-blur-2xl"
            >
              <ChevronLeft size={14} />
              Apps
            </motion.button>
          ) : null}

          <AnimatePresence mode="wait">
            <motion.div
              key={activeAppId ?? 'launcher'}
              initial={{ opacity: 0, y: 8, scale: 0.985 }}
              animate={{ opacity: 1, y: 0, scale: 1 }}
              exit={{ opacity: 0, y: -8, scale: 1.01 }}
              transition={SPRING_SNAP}
              className="h-full w-full"
            >
              {!activeApp ? (
                <AppGrid onLaunchApp={setActiveAppId} />
              ) : activeApp.id === 'drive_dashboard' ? (
                <TelemetryView />
              ) : (
                <PlaceholderApp appName={activeApp.name} />
              )}
            </motion.div>
          </AnimatePresence>
        </main>
      </div>
    </div>
  );
};
