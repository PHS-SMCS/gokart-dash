import React, { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { VIEWS, type ViewId } from '../constants/views';
import { useTelemetry } from '../hooks/useTelemetry';
import { StatusBar } from './StatusBar';
import { BottomDock } from './BottomDock';

export const DashboardLayout: React.FC = () => {
  const [activeView, setActiveView] = useState<ViewId>('drive');
  const telemetry = useTelemetry();
  const view = VIEWS.find((v) => v.id === activeView)!;

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
            {view.render({ telemetry })}
          </motion.div>
        </AnimatePresence>
      </main>

      <BottomDock active={activeView} onSelect={setActiveView} />
    </div>
  );
};
