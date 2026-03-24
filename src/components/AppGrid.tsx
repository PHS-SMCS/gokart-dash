import React from 'react';
import { APPS } from '../constants/apps';
import { AppIcon } from './AppIcon';

export const AppGrid: React.FC = () => {
  return (
    <div className="w-full h-full flex items-center bg-[#0a0a0a] overflow-hidden relative">
      {/* Background ambient gradient */}
      <div className="absolute inset-0 bg-gradient-to-br from-blue-900/10 via-black to-purple-900/10 pointer-events-none" />

      {/* Grid Container */}
      <div 
        className="w-full flex items-center gap-12 overflow-x-auto hide-scrollbar snap-x snap-mandatory px-24 py-12 pb-20 touch-pan-x"
        style={{ scrollBehavior: 'smooth' }}
      >
        {APPS.map((app) => (
          <AppIcon
            key={app.id}
            id={app.id}
            name={app.name}
            icon={app.icon}
            color={app.color}
            onClick={() => console.log(`Launched ${app.name}`)}
          />
        ))}
        {/* Spacer to allow the last item to snap to the center/left */}
        <div className="w-[10vw] shrink-0 pointer-events-none" />
      </div>
    </div>
  );
};
