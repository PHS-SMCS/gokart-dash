import React, { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { VIEWS, type ViewId } from '../constants/views';
import { useTelemetry } from '../telemetry/context';
import { useTelemetryNotifications } from '../notifications/useTelemetryNotifications';
import { StatusBar } from './StatusBar';
import { BottomDock } from './BottomDock';
import { DriveView } from './DriveView';
import { LightsView } from './LightsView';
import { MapView } from './MapView';
import { SystemView } from './SystemView';
import { Placeholder } from './Placeholder';

export const DashboardLayout: React.FC = () => {
  const [activeView, setActiveView] = useState<ViewId>('drive');
  const telemetry = useTelemetry();

  // Raise UI notifications from telemetry transitions (mounted once).
  useTelemetryNotifications(telemetry);

  return (
    <div className="flex h-screen w-screen flex-col overflow-hidden bg-[#080706] text-white">
      <StatusBar telemetry={telemetry} />

      <main className="relative flex-1 overflow-hidden">
        <AnimatePresence mode="wait">
          <motion.div
            key={activeView}
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.15 }}
            className="absolute inset-0"
          >
            {activeView === 'drive' ? (
              <DriveView telemetry={telemetry} />
            ) : activeView === 'map' ? (
              <MapView />
            ) : activeView === 'lights' ? (
              <LightsView />
            ) : activeView === 'system' ? (
              <SystemView />
            ) : (
              <Placeholder
                label={VIEWS.find((v) => v.id === activeView)!.label}
                icon={VIEWS.find((v) => v.id === activeView)!.icon}
              />
            )}
          </motion.div>
        </AnimatePresence>
      </main>

      <BottomDock active={activeView} onSelect={setActiveView} />
    </div>
  );
};
