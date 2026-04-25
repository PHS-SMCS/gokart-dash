import {
  Camera,
  Gauge,
  Lightbulb,
  Map,
  Settings2,
  type LucideIcon,
} from 'lucide-react';

export type ViewId = 'drive' | 'map' | 'camera' | 'lights' | 'system';

export interface ViewDef {
  id: ViewId;
  label: string;
  icon: LucideIcon;
}

export const VIEWS: ViewDef[] = [
  { id: 'drive', label: 'Drive', icon: Gauge },
  { id: 'map', label: 'Map', icon: Map },
  { id: 'camera', label: 'Camera', icon: Camera },
  { id: 'lights', label: 'Lights', icon: Lightbulb },
  { id: 'system', label: 'System', icon: Settings2 },
];
