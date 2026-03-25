import { Navigation, Gauge, Film, Camera, Radio, ShieldAlert } from 'lucide-react';

export const APPS = [
  {
    id: 'navigation',
    name: 'Navigation',
    details: 'Maps • Routes • ETA',
    icon: Navigation,
    color: 'from-blue-600 to-cyan-500',
  },
  {
    id: 'drive_dashboard',
    name: 'Drive Dashboard',
    details: 'Speed • RPM • Cruise • Perf',
    icon: Gauge,
    color: 'from-orange-500 to-red-600',
  },
  {
    id: 'media',
    name: 'Media',
    details: 'Music • Movies',
    icon: Film,
    color: 'from-purple-600 to-indigo-500',
  },
  {
    id: 'camera',
    name: 'Cameras',
    details: 'Front • Rear • Parking',
    icon: Camera,
    color: 'from-slate-600 to-slate-400',
  },
  {
    id: 'comms',
    name: 'Comms',
    details: 'Radio • Calls • Alerts',
    icon: Radio,
    color: 'from-teal-500 to-cyan-400',
  },
  {
    id: 'system',
    name: 'System',
    details: 'FX • Admin',
    icon: ShieldAlert,
    color: 'from-red-600 to-red-800',
  },
];
