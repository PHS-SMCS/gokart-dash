import type { RGB } from '../hooks/useLed';

export interface LightPreset {
  id: string;
  label: string;
  color: RGB;
}

export const LIGHT_PRESETS: LightPreset[] = [
  { id: 'white', label: 'White', color: { r: 255, g: 255, b: 255 } },
  { id: 'warm', label: 'Warm', color: { r: 255, g: 170, b: 80 } },
  { id: 'red', label: 'Red', color: { r: 255, g: 0, b: 0 } },
  { id: 'amber', label: 'Amber', color: { r: 255, g: 130, b: 0 } },
  { id: 'green', label: 'Green', color: { r: 0, g: 255, b: 30 } },
  { id: 'cyan', label: 'Cyan', color: { r: 0, g: 200, b: 255 } },
  { id: 'blue', label: 'Blue', color: { r: 30, g: 90, b: 255 } },
  { id: 'magenta', label: 'Magenta', color: { r: 255, g: 0, b: 160 } },
];
