import {
  Navigation,
  Gauge,
  Timer,
  Camera,
  Music,
  Film,
  Sparkles,
  Radio,
  ShieldAlert,
  Rocket
} from 'lucide-react';

export const APPS = [
  {
    id: 'navigation',
    name: 'Navigation',
    icon: Navigation,
    color: 'from-blue-600 to-cyan-500',
  },
  {
    id: 'dashboard',
    name: 'Dashboard',
    icon: Gauge,
    color: 'from-orange-500 to-red-600',
  },
  {
    id: 'cruise_control',
    name: 'Cruise Control',
    icon: Timer,
    color: 'from-green-500 to-emerald-400',
  },
  {
    id: 'perf_presets',
    name: 'Perf Presets',
    icon: Rocket,
    color: 'from-yellow-400 to-orange-500',
  },
  {
    id: 'camera',
    name: 'Camera',
    icon: Camera,
    color: 'from-slate-600 to-slate-400',
  },
  {
    id: 'music',
    name: 'Music',
    icon: Music,
    color: 'from-pink-500 to-rose-400',
  },
  {
    id: 'movies',
    name: 'Movies',
    icon: Film,
    color: 'from-purple-600 to-indigo-500',
  },
  {
    id: 'fx',
    name: 'FX',
    icon: Sparkles,
    color: 'from-fuchsia-500 to-pink-500',
  },
  {
    id: 'comms',
    name: 'Comms',
    icon: Radio,
    color: 'from-teal-500 to-cyan-400',
  },
  {
    id: 'admin',
    name: 'Admin',
    icon: ShieldAlert,
    color: 'from-red-600 to-red-800',
  },
];
