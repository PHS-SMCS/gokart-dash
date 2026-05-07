import { describe, expect, test } from 'vitest';
import { isValidElement } from 'react';
import { VIEWS, type ViewId } from './views';
import type { Telemetry } from '../hooks/useTelemetry';

const MOCK_TELEMETRY: Telemetry = {
  speedMph: 0,
  rpm: 0,
  rpmMax: 7000,
  rpmRedline: 6200,
  throttlePct: 0,
  brakePct: 0,
  gear: 'N',
  mode: 'SAFE',
  batteryPct: 84,
  motorTempF: 152,
  motorTempMaxF: 240,
  rangeMi: 18.4,
  gpsSats: 0,
  gpsFix: false,
  headlights: false,
  contactor: false,
  armed: false,
};

describe('VIEWS registry', () => {
  test('contains 5 entries', () => {
    expect(VIEWS).toHaveLength(5);
  });

  test('ids are exhaustive over ViewId', () => {
    const expectedIds: ViewId[] = ['drive', 'map', 'camera', 'lights', 'system'];
    const actualIds = VIEWS.map((v) => v.id).sort();
    expect(actualIds).toEqual(expectedIds.sort());
  });

  test('every entry has a non-empty label and an icon', () => {
    for (const v of VIEWS) {
      expect(typeof v.label).toBe('string');
      expect(v.label.length).toBeGreaterThan(0);
      expect(v.icon).toBeDefined();
    }
  });

  test('every entry has a render function returning a valid React element', () => {
    for (const v of VIEWS) {
      expect(typeof v.render).toBe('function');
      const element = v.render({ telemetry: MOCK_TELEMETRY });
      expect(isValidElement(element)).toBe(true);
    }
  });
});
