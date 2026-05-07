import type { ReactElement } from 'react';
import {
  Camera,
  Gauge,
  Lightbulb,
  Map,
  Settings2,
  type LucideIcon,
} from 'lucide-react';

import { DriveView } from '../components/DriveView';
import { LightsView } from '../components/LightsView';
import { Placeholder } from '../components/Placeholder';
import type { Telemetry } from '../hooks/useTelemetry';

export type ViewId = 'drive' | 'map' | 'camera' | 'lights' | 'system';

export interface ViewProps {
  telemetry: Telemetry;
}

export interface ViewDef {
  id: ViewId;
  label: string;
  icon: LucideIcon;
  render: (props: ViewProps) => ReactElement;
}

export const VIEWS: ViewDef[] = [
  {
    id: 'drive',
    label: 'Drive',
    icon: Gauge,
    render: ({ telemetry }) => <DriveView telemetry={telemetry} />,
  },
  {
    id: 'map',
    label: 'Map',
    icon: Map,
    render: () => <Placeholder label="Map" icon={Map} />,
  },
  {
    id: 'camera',
    label: 'Camera',
    icon: Camera,
    render: () => <Placeholder label="Camera" icon={Camera} />,
  },
  {
    id: 'lights',
    label: 'Lights',
    icon: Lightbulb,
    render: () => <LightsView />,
  },
  {
    id: 'system',
    label: 'System',
    icon: Settings2,
    render: () => <Placeholder label="System" icon={Settings2} />,
  },
];
