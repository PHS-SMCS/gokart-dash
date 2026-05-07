# React/TS Dashboard Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the hand-coded `if/else` view dispatch in `DashboardLayout` into a registry-driven approach, co-locate scattered utility helpers into `src/lib/`, drop the inline `BridgeStatus` redefinition in `LightsView`, fix the Vite scaffold leftover `package.json` name, and **bring test infrastructure into the project** along the way (Vitest + React Testing Library + jest-dom + jsdom). Every code change uses TDD (write failing test → red → minimal implementation → green → commit).

**Architecture:** Two new utility modules under `src/lib/` (`color.ts`, `math.ts`) own helpers previously inlined in `DriveView` and `LightsView`. The `RGB` type moves from `useLed.ts` to `lib/color.ts`. `views.ts` gains a `render: (props: ViewProps) => ReactElement` field; `DashboardLayout` becomes a registry lookup. Tests live next to source files (`*.test.ts(x)` co-located).

**Tech Stack:** React 19, TypeScript 5.9, Vite 8, Tailwind 3.4, Framer Motion 12, Lucide React. **NEW:** Vitest, `@testing-library/react`, `@testing-library/user-event`, `@testing-library/jest-dom`, jsdom.

**Reference:** Spec at [`docs/superpowers/specs/2026-05-07-react-refactor-design.md`](../specs/2026-05-07-react-refactor-design.md).

**Verification approach:** TDD throughout. The merge gate is `npm test` exits 0, `npm run build` exits 0, `npm run lint` exits 0, and a manual visual smoke in a dev-server browser session passes.

**TDD discipline notes:**
- For NEW modules (`lib/math`, `lib/color`): strict red→green. The test imports from a non-existent file; running it fails with "module not found" before the implementation lands.
- For NEW SHAPE on existing data (`VIEWS.render` field): the test asserts the new field exists; running it fails before the migration adds the field.
- For BEHAVIOR-PRESERVING REFACTORS (DashboardLayout dispatch): a *characterization test* is written first that captures existing behavior. To validate "watching it fail," temporarily comment out the dispatch in the source file, confirm the test fails, then uncomment. This proves the test detects breakage. The skill's "Watch it fail" step is satisfied via this deliberate-break technique.

---

## File Structure

**Create (new files):**
- `src/lib/color.ts` — `RGB` interface, `rgbToCss`, `scale`, `contrastColor`.
- `src/lib/color.test.ts` — Vitest unit tests for the above.
- `src/lib/math.ts` — `clamp01`.
- `src/lib/math.test.ts` — Vitest unit tests.
- `src/constants/views.test.tsx` — Vitest test for the registry shape.
- `src/components/DashboardLayout.test.tsx` — RTL test for view dispatch.
- `src/test/setup.ts` — Vitest setup (imports jest-dom matchers).

**Modify:**
- `vite.config.ts` — add Vitest `test` block.
- `package.json` — add `test`/`test:watch` scripts; add devDependencies; rename `name`.
- `src/hooks/useLed.ts` — drop inline `RGB` interface, import from `lib/color`. `BridgeStatus` stays as the canonical export.
- `src/constants/lightPresets.ts` — switch `RGB` import from `useLed` to `lib/color`.
- `src/components/LightsView.tsx` — drop inline color helpers; import from `lib/color`. Drop inline `BridgeStatus` union; import from `useLed`.
- `src/components/DriveView.tsx` — drop inline `clamp01`; import from `lib/math`.
- `src/constants/views.ts` → `src/constants/views.tsx` (renamed; gains `render` field; entries point at view components).
- `src/components/DashboardLayout.tsx` — drop the local `renderView`; inline a `VIEWS.find(...)!.render(...)` lookup.

**Delete:** None.

---

## Task 1: Set up test infrastructure (Vitest + RTL + jest-dom + jsdom)

Bring the test stack online. After this task, `npm test` runs Vitest with jsdom and `@testing-library/jest-dom` matchers available globally.

**Files:**
- Modify: `package.json`, `vite.config.ts`
- Create: `src/test/setup.ts`, `src/test/smoke.test.ts` (deleted at end of task)

- [ ] **Step 1: Install dev dependencies**

```bash
npm install -D vitest @testing-library/react @testing-library/user-event @testing-library/jest-dom jsdom
```

Expected: completes successfully. New entries appear under `devDependencies` in `package.json` and `package-lock.json` updates.

- [ ] **Step 2: Add `test` and `test:watch` scripts to `package.json`**

Find the `"scripts"` block:

```json
"scripts": {
  "dev": "vite",
  "build": "tsc -b && vite build",
  "lint": "eslint .",
  "preview": "vite preview"
},
```

Replace with:

```json
"scripts": {
  "dev": "vite",
  "build": "tsc -b && vite build",
  "lint": "eslint .",
  "preview": "vite preview",
  "test": "vitest run",
  "test:watch": "vitest"
},
```

- [ ] **Step 3: Create `src/test/setup.ts`**

```ts
import '@testing-library/jest-dom/vitest';
```

This enables matchers like `toBeInTheDocument()`, `toHaveTextContent()`, etc. on Vitest's `expect`.

- [ ] **Step 4: Extend `vite.config.ts` with a Vitest `test` block**

The current file is:

```ts
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
})
```

Replace with:

```ts
/// <reference types="vitest/config" />
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test/setup.ts'],
  },
})
```

The `/// <reference types="vitest/config" />` triple-slash directive teaches TypeScript about the `test` block on the vite config object. `globals: true` makes `describe`/`test`/`expect` available without explicit imports (matches the convention most TDD examples use), but importing from `vitest` directly also works.

- [ ] **Step 5: Create a smoke test to prove the toolchain works**

`src/test/smoke.test.ts`:

```ts
import { describe, expect, test } from 'vitest';

describe('test toolchain smoke', () => {
  test('arithmetic still works', () => {
    expect(1 + 1).toBe(2);
  });

  test('jest-dom matchers are loaded', () => {
    const div = document.createElement('div');
    div.textContent = 'hello';
    document.body.appendChild(div);
    expect(div).toHaveTextContent('hello');
    document.body.removeChild(div);
  });
});
```

- [ ] **Step 6: Run the smoke test**

```bash
npm test
```

Expected: 2 tests pass, exit 0. The `jest-dom` `toHaveTextContent` matcher confirms the setup file loaded correctly.

If this fails, the test infra setup itself is broken — fix before continuing. Do NOT proceed to subsequent tasks.

- [ ] **Step 7: Delete the smoke test**

It served its purpose. Delete `src/test/smoke.test.ts`. (Keep `src/test/setup.ts` — it's referenced by `vite.config.ts`.)

- [ ] **Step 8: Verify `npm test` still runs (with no tests now)**

```bash
npm test
```

Expected: Vitest reports "No test files found" (or similar) and exits with a non-zero code OR reports zero tests passing. Either is acceptable for now since we're about to add real tests in Task 2.

- [ ] **Step 9: Verify build and lint still pass**

```bash
npm run build
npm run lint
```

Both must exit 0. The `vite.config.ts` triple-slash directive needs to resolve; `setup.ts` needs to compile.

- [ ] **Step 10: Commit**

```bash
git add package.json package-lock.json vite.config.ts src/test/setup.ts
git commit -m "feat(dash): set up vitest + react-testing-library + jest-dom

Adds devDependencies (vitest, @testing-library/react,
@testing-library/user-event, @testing-library/jest-dom, jsdom) and
configures jsdom environment via vite.config.ts. New 'test' and
'test:watch' npm scripts. setup.ts loads jest-dom matchers globally.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: TDD `src/lib/math.ts`

Write the test for `clamp01` first. The test imports from `./math`, which does not exist yet — so the test fails with a module-resolution error. Then create the file with the minimal implementation; the test passes.

**Files:**
- Create: `src/lib/math.test.ts`, then `src/lib/math.ts`

- [ ] **Step 1: Write the failing test**

Create `src/lib/math.test.ts`:

```ts
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
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
npm test src/lib/math.test.ts
```

Expected: failure with a module-resolution error like `Failed to resolve import "./math" from "src/lib/math.test.ts"`. This is the correct red — the test fails because the implementation doesn't exist yet.

- [ ] **Step 3: Create the minimal implementation**

`src/lib/math.ts`:

```ts
export function clamp01(n: number): number {
  return Math.min(1, Math.max(0, n));
}
```

- [ ] **Step 4: Run the test, confirm it passes**

```bash
npm test src/lib/math.test.ts
```

Expected: 3 tests pass, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/lib/math.ts src/lib/math.test.ts
git commit -m "feat(dash): add lib/math.clamp01 (TDD)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: TDD `src/lib/color.ts`

Same TDD shape as Task 2: test first, fail, implement, pass.

**Files:**
- Create: `src/lib/color.test.ts`, then `src/lib/color.ts`

- [ ] **Step 1: Write the failing test**

Create `src/lib/color.test.ts`:

```ts
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
    // 33% of 100 = 33; 33% of 50 = 16.5 -> Math.round(16.5) = 17 (JS rounds .5 up for positive)
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
    // Per (0.299*R + 0.587*G + 0.114*B) / 255:
    //   red    -> 0.299
    //   green  -> 0.587
    //   blue   -> 0.114
    // All <= 0.6, so all return '#ffffff'.
    expect(contrastColor({ r: 255, g: 0, b: 0 })).toBe('#ffffff');
    expect(contrastColor({ r: 0, g: 255, b: 0 })).toBe('#ffffff');
    expect(contrastColor({ r: 0, g: 0, b: 255 })).toBe('#ffffff');
  });

  test('warm preset color (RGB 255, 170, 80) returns dark text', () => {
    // Luminance ~= 0.726, > 0.6.
    expect(contrastColor({ r: 255, g: 170, b: 80 })).toBe('#0a0a0a');
  });
});
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
npm test src/lib/color.test.ts
```

Expected: module-resolution failure — `./color` doesn't exist.

- [ ] **Step 3: Create the minimal implementation**

`src/lib/color.ts`:

```ts
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
```

- [ ] **Step 4: Run the test, confirm it passes**

```bash
npm test src/lib/color.test.ts
```

Expected: all tests pass (rgbToCss: 1 test, scale: 6 tests, contrastColor: 4 tests = 11 assertions, varying test count by grouping).

- [ ] **Step 5: Run full test suite to confirm no other tests broke**

```bash
npm test
```

Expected: all tests across all files pass.

- [ ] **Step 6: Commit**

```bash
git add src/lib/color.ts src/lib/color.test.ts
git commit -m "feat(dash): add lib/color helpers (TDD)

RGB interface, rgbToCss, scale, contrastColor with full unit-test
coverage. Behavior matches the existing inlined helpers in LightsView,
which migrate to use this module in a later task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Migrate `src/hooks/useLed.ts` to import `RGB` from `lib/color`

Type-only refactor. No new test — `tsc -b` is the gate. The existing `lib/color` tests cover `RGB` semantically.

**Files:**
- Modify: `src/hooks/useLed.ts`

- [ ] **Step 1: Read `src/hooks/useLed.ts`** to confirm the current `RGB` interface and import section.

- [ ] **Step 2: Replace the imports + RGB interface block**

Find lines 1–7:

```ts
import { useCallback, useEffect, useRef, useState } from 'react';

export interface RGB {
  r: number;
  g: number;
  b: number;
}
```

Replace with:

```ts
import { useCallback, useEffect, useRef, useState } from 'react';
import type { RGB } from '../lib/color';

export type { RGB };
```

The `export type { RGB }` re-export keeps backwards compatibility for any consumer doing `import { type RGB } from '...useLed'`. Tasks 5 and 7 update the only two such consumers; the re-export is cheap insurance.

- [ ] **Step 3: Verify build, lint, and tests still pass**

```bash
npm run build
npm test
```

Both must exit 0. The build catches type errors; the tests confirm no behavior regression in `lib/color` (which now backs `useLed`'s `RGB`).

- [ ] **Step 4: Commit**

```bash
git add src/hooks/useLed.ts
git commit -m "refactor(dash): import RGB from lib/color in useLed

The RGB type now lives in lib/color (where the helpers that operate on
it live). useLed re-exports it for backwards compatibility.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Switch `src/constants/lightPresets.ts` to import `RGB` from `lib/color`

Type-only refactor.

**Files:**
- Modify: `src/constants/lightPresets.ts`

- [ ] **Step 1: Replace the import**

Find line 1:

```ts
import type { RGB } from '../hooks/useLed';
```

Replace with:

```ts
import type { RGB } from '../lib/color';
```

- [ ] **Step 2: Verify**

```bash
npm run build
npm test
```

- [ ] **Step 3: Commit**

```bash
git add src/constants/lightPresets.ts
git commit -m "refactor(dash): import RGB from lib/color in lightPresets

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Migrate `src/components/DriveView.tsx` to import `clamp01` from `lib/math`

The `lib/math` tests already cover `clamp01`'s behavior — the migration changes the import path but leaves call sites identical. No new test needed.

**Files:**
- Modify: `src/components/DriveView.tsx`

- [ ] **Step 1: Add the import**

Find this block at the top (around lines 1–5):

```tsx
import React from 'react';
import { motion } from 'framer-motion';
import { Battery, Lightbulb, Plug, Power, Thermometer } from 'lucide-react';
import { SPRING_SNAP } from '../constants/motion';
import type { Telemetry } from '../hooks/useTelemetry';
```

Replace with:

```tsx
import React from 'react';
import { motion } from 'framer-motion';
import { Battery, Lightbulb, Plug, Power, Thermometer } from 'lucide-react';
import { SPRING_SNAP } from '../constants/motion';
import { clamp01 } from '../lib/math';
import type { Telemetry } from '../hooks/useTelemetry';
```

- [ ] **Step 2: Delete the local `clamp01` definition**

Find and delete (currently at the bottom, around lines 159–161):

```tsx
function clamp01(n: number) {
  return Math.min(1, Math.max(0, n));
}
```

- [ ] **Step 3: Verify**

```bash
npm run build
npm test
```

The 4 call sites (`rpmPct`, `redlinePct`, `motorTempPct`, and `PedalBar`'s `clamp01(pct / 100)`) now resolve to the imported function.

- [ ] **Step 4: Commit**

```bash
git add src/components/DriveView.tsx
git commit -m "refactor(dash): use lib/math.clamp01 in DriveView

Drops the local clamp01 helper; the imported version is identical and
covered by lib/math.test.ts.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Migrate `src/components/LightsView.tsx` to use `lib/color` helpers and the canonical `BridgeStatus` type

Three small moves: import color helpers from `lib/color`, drop their inline definitions, and replace the inline `'unknown' | 'ok' | 'error'` union in `Header` with the `BridgeStatus` import. The `lib/color` tests already cover the helpers' behavior.

**Files:**
- Modify: `src/components/LightsView.tsx`

- [ ] **Step 1: Replace the import block + remove inline `rgbToCss`/`scale`**

Find lines 1–22:

```tsx
import React, { useCallback, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { Power, WifiOff } from 'lucide-react';
import { LIGHT_PRESETS } from '../constants/lightPresets';
import { SPRING_SNAP } from '../constants/motion';
import { useLed, type RGB } from '../hooks/useLed';

const DEFAULT_PRESET_ID = 'white';
const DEFAULT_BRIGHTNESS = 80;

function rgbToCss({ r, g, b }: RGB): string {
  return `rgb(${r}, ${g}, ${b})`;
}

function scale(rgb: RGB, brightness: number): RGB {
  const f = Math.min(100, Math.max(0, brightness)) / 100;
  return {
    r: Math.round(rgb.r * f),
    g: Math.round(rgb.g * f),
    b: Math.round(rgb.b * f),
  };
}
```

Replace with:

```tsx
import React, { useCallback, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { Power, WifiOff } from 'lucide-react';
import { LIGHT_PRESETS } from '../constants/lightPresets';
import { SPRING_SNAP } from '../constants/motion';
import { contrastColor, rgbToCss, scale } from '../lib/color';
import { useLed, type BridgeStatus } from '../hooks/useLed';

const DEFAULT_PRESET_ID = 'white';
const DEFAULT_BRIGHTNESS = 80;
```

- [ ] **Step 2: Delete the local `contrastColor` definition**

Find and delete (currently around lines 144–148, after Step 1's edits the line numbers shift):

```tsx
function contrastColor({ r, g, b }: RGB): string {
  // Perceived luminance — white text on dark colors, black on light.
  const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  return lum > 0.6 ? '#0a0a0a' : '#ffffff';
}
```

- [ ] **Step 3: Replace the inline `BridgeStatus` union in the local `Header` component**

Find this declaration (currently around lines 81–84):

```tsx
const Header: React.FC<{ status: 'unknown' | 'ok' | 'error'; pending: boolean }> = ({
  status,
  pending,
}) => {
```

Replace with:

```tsx
const Header: React.FC<{ status: BridgeStatus; pending: boolean }> = ({
  status,
  pending,
}) => {
```

- [ ] **Step 4: Verify**

```bash
npm run build
npm test
```

- [ ] **Step 5: Commit**

```bash
git add src/components/LightsView.tsx
git commit -m "refactor(dash): use lib/color helpers and BridgeStatus type in LightsView

Drops the local rgbToCss/scale/contrastColor copies and the inline
'unknown' | 'ok' | 'error' union. Imports the canonical versions from
lib/color and useLed.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: TDD `VIEWS` registry shape — add `render` field

This task is genuine red→green TDD: the test asserts that each `VIEWS` entry has a `render` function and that `VIEWS.find(v => v.id === 'drive').render({telemetry})` produces a valid React element. The current `VIEWS` has no `render` field, so the test fails. The migration adds the field, and the test passes.

**Note:** The file is renamed from `views.ts` to `views.tsx` because the new content includes JSX.

**Files:**
- Create: `src/constants/views.test.tsx`
- Rename: `src/constants/views.ts` → `src/constants/views.tsx` (with content changes)

- [ ] **Step 1: Write the failing test**

Create `src/constants/views.test.tsx`:

```tsx
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
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
npm test src/constants/views.test.tsx
```

Expected: failures on the `render` assertions — `typeof v.render` is `'undefined'`, not `'function'`. The first three tests (length, ids, label/icon) should pass against the current code. The fourth test fails with messages like `expected 'undefined' to be 'function'`.

This is correct red.

- [ ] **Step 3: Rename the file and add the `render` field**

Use `git mv` to preserve rename history:

```bash
git mv src/constants/views.ts src/constants/views.tsx
```

Then overwrite the renamed file with the new content. `src/constants/views.tsx`:

```tsx
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
```

- [ ] **Step 4: Run the test, confirm it passes**

```bash
npm test src/constants/views.test.tsx
```

Expected: all 4 tests pass.

- [ ] **Step 5: Run full test suite + build**

```bash
npm test
npm run build
```

Both must exit 0.

The two consumers of `views` (`DashboardLayout.tsx` and `BottomDock.tsx`) import from `'../constants/views'` (no extension) — that resolves to `views.tsx` automatically once `views.ts` is gone. The hand-coded `renderView` in `DashboardLayout.tsx` still works because it doesn't read the new `render` field; Task 9 removes it.

- [ ] **Step 6: Commit**

```bash
git add src/constants/views.test.tsx src/constants/views.tsx
# git mv has already staged the deletion of views.ts
git commit -m "feat(dash): make VIEWS a renderer-carrying registry (TDD)

Each entry now carries its own render fn keyed on ViewId. Renames
views.ts -> views.tsx since render fns return JSX. New views.test.tsx
covers registry shape and per-entry render() validity.

DashboardLayout still uses the hand-coded renderView helper; the next
task removes it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: TDD `DashboardLayout` — characterize then refactor dispatch

The behavior is the same before and after the refactor (the user-visible dispatch routes the same way). The test is therefore a **characterization test** — it captures the current behavior, runs green against the existing code, and continues to run green after the refactor. To validate "watching it fail" (TDD discipline), we deliberately break the dispatch in the source, confirm the test fails, then revert and proceed.

**Files:**
- Create: `src/components/DashboardLayout.test.tsx`
- Modify: `src/components/DashboardLayout.tsx`

- [ ] **Step 1: Write the characterization test**

Create `src/components/DashboardLayout.test.tsx`:

```tsx
import { afterEach, describe, expect, test, vi } from 'vitest';
import { cleanup, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { DashboardLayout } from './DashboardLayout';

// Mock useLed since LightsView calls it on mount and would attempt a fetch
// against a bridge that doesn't exist in the test environment.
vi.mock('../hooks/useLed', () => ({
  useLed: () => ({ status: 'unknown', pending: false, send: vi.fn() }),
}));

describe('DashboardLayout', () => {
  afterEach(() => cleanup());

  test('renders the Drive view by default', () => {
    render(<DashboardLayout />);
    // DriveView contains "MPH" text under the speed numeral.
    expect(screen.getByText('MPH')).toBeInTheDocument();
  });

  test('clicking Lights in the bottom dock shows the lights view', async () => {
    const user = userEvent.setup();
    render(<DashboardLayout />);

    const lightsButton = screen.getByRole('button', { name: /lights/i });
    await user.click(lightsButton);

    // LightsView's BrightnessSlider has the "Brightness" label.
    expect(await screen.findByText(/Brightness/i)).toBeInTheDocument();
  });

  test('clicking Map shows the placeholder', async () => {
    const user = userEvent.setup();
    render(<DashboardLayout />);

    const mapButton = screen.getByRole('button', { name: /map/i });
    await user.click(mapButton);

    // Placeholder renders "Coming soon".
    expect(await screen.findByText(/Coming soon/i)).toBeInTheDocument();
  });

  test('clicking System shows the placeholder', async () => {
    const user = userEvent.setup();
    render(<DashboardLayout />);

    const systemButton = screen.getByRole('button', { name: /system/i });
    await user.click(systemButton);

    expect(await screen.findByText(/Coming soon/i)).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run the test against the current (pre-refactor) code, confirm it passes**

```bash
npm test src/components/DashboardLayout.test.tsx
```

Expected: all 4 tests pass. The current hand-coded `renderView` correctly dispatches each view ID.

If a test fails at this stage, investigate before proceeding — the test is wrong about the existing behavior, or `framer-motion`'s `AnimatePresence` is causing flakiness in jsdom. For flakiness, common fixes:
- Use `await screen.findByText(...)` (which already does — `findBy*` waits up to 1s).
- If still flaky, mock `framer-motion` at the top of the test file:
  ```tsx
  vi.mock('framer-motion', async () => {
    const actual = await vi.importActual('framer-motion');
    return {
      ...actual,
      AnimatePresence: ({ children }: { children: React.ReactNode }) => <>{children}</>,
    };
  });
  ```

- [ ] **Step 3: Validate the test detects breakage (deliberate-break technique)**

Edit `src/components/DashboardLayout.tsx` and temporarily break the dispatch. Find the `renderView` function:

```tsx
function renderView(id: ViewId, telemetry: ReturnType<typeof useTelemetry>) {
  if (id === 'drive') return <DriveView telemetry={telemetry} />;
  if (id === 'lights') return <LightsView />;
  const def = VIEWS.find((v) => v.id === id)!;
  return <Placeholder label={def.label} icon={def.icon} />;
}
```

Temporarily change it to always return `null`:

```tsx
function renderView(id: ViewId, telemetry: ReturnType<typeof useTelemetry>) {
  return null;
}
```

Run the tests:

```bash
npm test src/components/DashboardLayout.test.tsx
```

Expected: tests fail (assertions for "MPH", "Brightness", "Coming soon" all fail because nothing renders). This confirms the test detects breakage.

Now revert the deliberate break (restore the original `renderView` function body):

```tsx
function renderView(id: ViewId, telemetry: ReturnType<typeof useTelemetry>) {
  if (id === 'drive') return <DriveView telemetry={telemetry} />;
  if (id === 'lights') return <LightsView />;
  const def = VIEWS.find((v) => v.id === id)!;
  return <Placeholder label={def.label} icon={def.icon} />;
}
```

Run again:

```bash
npm test src/components/DashboardLayout.test.tsx
```

Expected: passes again.

- [ ] **Step 4: Refactor `DashboardLayout` to use the registry**

Overwrite `src/components/DashboardLayout.tsx` with this content:

```tsx
import React, { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { VIEWS, type ViewId } from '../constants/views';
import { useTelemetry } from '../hooks/useTelemetry';
import { StatusBar } from './StatusBar';
import { BottomDock } from './BottomDock';

export const DashboardLayout: React.FC = () => {
  const [activeView, setActiveView] = useState<ViewId>('drive');
  const telemetry = useTelemetry();
  const view = VIEWS.find((v) => v.id === activeView)!;

  return (
    <div className="flex h-screen w-screen flex-col overflow-hidden bg-[#080706] text-white">
      <StatusBar telemetry={telemetry} />

      <main className="relative flex-1 overflow-hidden">
        <AnimatePresence mode="wait">
          <motion.div
            key={activeView}
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.15 }}
            className="absolute inset-0"
          >
            {view.render({ telemetry })}
          </motion.div>
        </AnimatePresence>
      </main>

      <BottomDock active={activeView} onSelect={setActiveView} />
    </div>
  );
};
```

What's removed:
- Imports of `DriveView`, `LightsView`, `Placeholder` (now imported inside `views.tsx`).
- The local `renderView` helper.

What's added:
- `const view = VIEWS.find((v) => v.id === activeView)!;`
- `{view.render({ telemetry })}` inline in the JSX.

- [ ] **Step 5: Run the test, confirm it still passes**

```bash
npm test src/components/DashboardLayout.test.tsx
```

Expected: all 4 tests pass. The behavior is preserved.

- [ ] **Step 6: Run full test suite + build + lint**

```bash
npm test
npm run build
npm run lint
```

All three must exit 0.

- [ ] **Step 7: Commit**

```bash
git add src/components/DashboardLayout.tsx src/components/DashboardLayout.test.tsx
git commit -m "refactor(dash): replace renderView if/else with VIEWS registry lookup

Adding a real view is now a single edit in views.tsx — replace the
Placeholder render fn with the actual component. DashboardLayout no
longer needs to know about specific view types.

Characterization tests in DashboardLayout.test.tsx capture the dispatch
behavior (default Drive, click Lights/Map/System) and were validated
to detect breakage via a deliberate-break sanity check before the
refactor.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Fix `package.json` `name`

Cosmetic. No test.

**Files:**
- Modify: `package.json`

- [ ] **Step 1: Edit `package.json`**

Find line 2:

```json
  "name": "temp-app",
```

Replace with:

```json
  "name": "gokart-dash",
```

- [ ] **Step 2: Verify**

```bash
npm run build
npm test
```

Both must exit 0. `package-lock.json` may also pick up a `"name"` change at its top level; if so, stage it too.

- [ ] **Step 3: Commit**

```bash
git add package.json package-lock.json
git commit -m "chore(dash): rename package from temp-app to gokart-dash

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Final verification — full test suite + build + lint + dev-server smoke

The merge gate.

**Files:**
- Read-only verification.

- [ ] **Step 1: Full test suite**

```bash
npm test
```

Expected: every test file passes, exit 0. Capture the test count for the PR description.

- [ ] **Step 2: Static build**

```bash
npm run build
```

Expected: exit 0. The build artifact lands in `dist/`.

- [ ] **Step 3: Lint**

```bash
npm run lint
```

Expected: exit 0. (Pre-existing warnings, if any, should be captured in the PR description as out-of-scope.)

- [ ] **Step 4: Dev-server smoke**

Start the dev server in the background (or in another terminal):

```bash
npm run dev
```

Open `http://localhost:5173` in a browser at the standard 800×480 kiosk viewport. The Lights view will say "Bridge offline" since no Pi is running locally — that's expected. Verify:

- [ ] **Drive view (default)** — animates: speed/RPM/throttle move; gear flips between N and D; motor temp drifts; battery + range pills render.
- [ ] **Bottom dock** — five items: Drive, Map, Camera, Lights, System. Tapping each switches the main panel; transitions are smooth; the small underline indicator slides to the active tab.
- [ ] **Map / Camera / System** — show the Placeholder with the appropriate icon and label and "Coming soon".
- [ ] **Lights view** — header shows "Bridge offline" badge (red); 4×2 grid of preset swatches; tapping a preset toggles it active; brightness slider drag from 0–100%; power toggle off → all swatches dim, on → swatches use current brightness.
- [ ] **Status bar** — current time displayed; mode label; armed/disarmed pill; GPS sat count; battery percent.

If anything looks different from pre-refactor, STOP and investigate.

- [ ] **Step 5: Stop the dev server**

Kill the background dev-server process (Ctrl+C in its terminal, or `kill` the pid).

- [ ] **Step 6: No commit needed for verification.**

If all four checks pass, the React/TS sub-project is done.

---

## Done criteria

- All 11 tasks above have their checkboxes ticked.
- `npm test` exits 0 (Vitest reports tests for `lib/math`, `lib/color`, `views`, `DashboardLayout`).
- `npm run build` exits 0.
- `npm run lint` exits 0.
- Dev-server visual smoke passes for all 5 views and the status bar / bottom dock.
- `git log main..HEAD --stat` shows: 2 new files in `src/lib/`, 4 new test files, 1 new setup file, modifications across `src/components/`, `src/constants/`, `src/hooks/`, `package.json`, `package-lock.json`, `vite.config.ts`. No changes to `hardware-scripts/`, `deploy/`, or any file outside `src/`, `package.json`, `package-lock.json`, and `vite.config.ts` (other than this plan and the spec doc).

When all of the above hold, the React/TS sub-project is complete. The next sub-project is the Teensy firmware refactor — its own spec, plan, branch, and PR.
