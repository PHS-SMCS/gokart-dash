import { describe, expect, test } from 'vitest';
import { clamp01 } from './math';

describe('clamp01', () => {
  test('passes through values inside [0,1]', () => {
    expect(clamp01(0)).toBe(0);
    expect(clamp01(0.5)).toBe(0.5);
    expect(clamp01(1)).toBe(1);
  });

  test('clamps values above 1 to 1', () => {
    expect(clamp01(1.5)).toBe(1);
    expect(clamp01(100)).toBe(1);
  });

  test('clamps negative values to 0', () => {
    expect(clamp01(-0.1)).toBe(0);
    expect(clamp01(-100)).toBe(0);
  });
});
