import React, { useState, useEffect } from 'react';
import { Home, Grid, CircleGauge } from 'lucide-react';
import { motion } from 'framer-motion';

export const SidebarDock: React.FC = () => {
  const [time, setTime] = useState<Date>(new Date());
  
  // Update time every minute
  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 60000);
    return () => clearInterval(timer);
  }, []);

  const formatTime = (date: Date) => {
    return date.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  };

  return (
    <div className="w-[15%] h-full bg-[#121212] border-r border-white/10 flex flex-col items-center justify-between py-6 shrink-0 relative z-10 shadow-2xl shadow-black">
      
      {/* Top Section: Time & Speedometer */}
      <div className="flex flex-col items-center gap-6 w-full px-2">
        {/* Current Time */}
        <div className="text-white text-2xl md:text-3xl font-bold tracking-wider drop-shadow-md text-center w-full">
          {formatTime(time)}
        </div>

        {/* Speedometer Widget */}
        <div className="flex flex-col items-center justify-center bg-black/40 w-20 h-20 md:w-24 md:h-24 rounded-full border-2 border-cyan-500/50 shadow-[0_0_15px_rgba(6,182,212,0.3)]">
          <CircleGauge size={24} className="text-cyan-400 mb-1 opacity-80" />
          <div className="text-white text-xl md:text-2xl font-black leading-none tracking-tighter">0</div>
          <div className="text-cyan-500 text-[10px] md:text-xs font-semibold uppercase mt-0.5 tracking-widest">MPH</div>
        </div>
      </div>

      {/* Bottom Section: Controls */}
      <div className="flex flex-col items-center gap-6 w-full mb-4 px-2">
        <motion.button 
          whileTap={{ scale: 0.9 }}
          className="w-16 h-16 md:w-20 md:h-20 bg-white/5 hover:bg-white/10 border border-white/10 rounded-2xl flex items-center justify-center transition-colors shadow-lg"
          onClick={() => console.log('Home Pressed')}
        >
          <Home size={28} className="text-white drop-shadow-sm" />
        </motion.button>
        
        <motion.button 
          whileTap={{ scale: 0.9 }}
          className="w-16 h-16 md:w-20 md:h-20 bg-white/5 hover:bg-white/10 border border-white/10 rounded-2xl flex items-center justify-center transition-colors shadow-lg"
          onClick={() => console.log('App Drawer Pressed')}
        >
          <Grid size={28} className="text-white drop-shadow-sm" />
        </motion.button>
      </div>

    </div>
  );
};
