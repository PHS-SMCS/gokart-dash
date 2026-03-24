import React from 'react';
import { SidebarDock } from './SidebarDock';
import { AppGrid } from './AppGrid';

export const DashboardLayout: React.FC = () => {
  return (
    <div className="w-screen h-screen bg-black overflow-hidden flex select-none font-sans text-white touch-none overscroll-none">
      <SidebarDock />
      <main className="w-[85%] h-full relative z-0">
        <AppGrid />
      </main>
    </div>
  );
};
