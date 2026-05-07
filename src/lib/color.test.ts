import { describe, expect, test } from 'vitest';
import { contrastColor, rgbToCss, scale } from './color';

describe('rgbToCss', () => {
  test('formats RGB as a CSS rgb() string', () => {
    expect(rgbToCss({ r: 255, g: 0, b: 0 })).toBe('rgb(255, 0, 0)');
    expect(rgbToCss({ r: 0, g: 128, b: 255 })).toBe('rgb(0, 128, 255)');
  });
});

describe('scale', () => {
  test('returns the original color at 100% brightness', () => {
    expect(scale({ r: 200, g: 100, b: 50 }, 100)).toEqual({ r: 200, g: 100, b: 50 });
  });

  test('returns black at 0% brightness', () => {
    expect(scale({ r: 200, g: 100, b: 50 }, 0)).toEqual({ r: 0, g: 0, b: 0 });
  });

  test('halves channel values at 50% brightness', () => {
    expect(scale({ r: 200, g: 100, b: 50 }, 50)).toEqual({ r: 100, g: 50, b: 25 });
  });

  test('clamps brightness above 100', () => {
    expect(scale({ r: 200, g: 100, b: 50 }, 150)).toEqual({ r: 200, g: 100, b: 50 });
  });

  test('clamps brightness below 0', () => {
    expect(scale({ r: 200, g: 100, b: 50 }, -10)).toEqual({ r: 0, g: 0, b: 0 });
  });

  test('rounds channel values to nearest integer', () => {
    expect(scale({ r: 100, g: 50, b: 0 }, 33)).toEqual({ r: 33, g: 17, b: 0 });
  });
});

describe('contrastColor', () => {
  test('returns dark text for very bright colors', () => {
    expect(contrastColor({ r: 255, g: 255, b: 255 })).toBe('#0a0a0a');
    expect(contrastColor({ r: 255, g: 220, b: 200 })).toBe('#0a0a0a');
  });

  test('returns light text for dark colors', () => {
    expect(contrastColor({ r: 0, g: 0, b: 0 })).toBe('#ffffff');
    expect(contrastColor({ r: 30, g: 90, b: 255 })).toBe('#ffffff');
  });

  test('uses perceived luminance: pure red/green/blue all return light text', () => {
    expect(contrastColor({ r: 255, g: 0, b: 0 })).toBe('#ffffff');
    expect(contrastColor({ r: 0, g: 255, b: 0 })).toBe('#ffffff');
    expect(contrastColor({ r: 0, g: 0, b: 255 })).toBe('#ffffff');
  });

  test('warm preset color (RGB 255, 170, 80) returns dark text', () => {
    expect(contrastColor({ r: 255, g: 170, b: 80 })).toBe('#0a0a0a');
  });
});
