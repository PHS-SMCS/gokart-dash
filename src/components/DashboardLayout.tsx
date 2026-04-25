import React, { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { VIEWS, type ViewId } from '../constants/views';
import { useTelemetry } from '../hooks/useTelemetry';
import { StatusBar } from './StatusBar';
import { BottomDock } from './BottomDock';
import { DriveView } from './DriveView';
import { LightsView } from './LightsView';
import { Placeholder } from './Placeholder';

export const DashboardLayout: React.FC = () => {
  const [activeView, setActiveView] = useState<ViewId>('drive');
  const telemetry = useTelemetry();

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
            {renderView(activeView, telemetry)}
          </motion.div>
        </AnimatePresence>
      </main>

      <BottomDock active={activeView} onSelect={setActiveView} />
    </div>
  );
};

function renderView(id: ViewId, telemetry: ReturnType<typeof useTelemetry>) {
  if (id === 'drive') return <DriveView telemetry={telemetry} />;
  if (id === 'lights') return <LightsView />;
  const def = VIEWS.find((v) => v.id === id)!;
  return <Placeholder label={def.label} icon={def.icon} />;
}
