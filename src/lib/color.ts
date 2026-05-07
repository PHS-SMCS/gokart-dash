export interface RGB {
  r: number;
  g: number;
  b: number;
}

export function rgbToCss({ r, g, b }: RGB): string {
  return `rgb(${r}, ${g}, ${b})`;
}

export function scale(rgb: RGB, brightness: number): RGB {
  const f = Math.min(100, Math.max(0, brightness)) / 100;
  return {
    r: Math.round(rgb.r * f),
    g: Math.round(rgb.g * f),
    b: Math.round(rgb.b * f),
  };
}

export function contrastColor({ r, g, b }: RGB): string {
  // Perceived luminance — white text on dark colors, black on light.
  const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  return lum > 0.6 ? '#0a0a0a' : '#ffffff';
}
