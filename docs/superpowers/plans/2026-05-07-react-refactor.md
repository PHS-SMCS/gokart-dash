# React/TS Dashboard Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the hand-coded `if/else` view dispatch in `DashboardLayout` into a registry-driven approach (each `VIEWS` entry carries its own render fn), co-locate scattered utility helpers into `src/lib/`, drop the inline `BridgeStatus` redefinition in `LightsView`, and fix the Vite scaffold leftover `package.json` name — without changing visual or behavioral output.

**Architecture:** Two new utility modules under `src/lib/` (`color.ts`, `math.ts`) own helpers that were previously inlined in `DriveView` and `LightsView`. The `RGB` type moves from `useLed.ts` to `lib/color.ts` (so `lightPresets.ts` no longer imports types from a hook). `views.ts` gains a `render: (props: ViewProps) => ReactElement` field; `DashboardLayout` becomes a registry lookup.

**Tech Stack:** React 19, TypeScript 5.9, Vite 8, Tailwind 3.4, Framer Motion 12, Lucide React.

**Reference:** Spec at [`docs/superpowers/specs/2026-05-07-react-refactor-design.md`](../specs/2026-05-07-react-refactor-design.md).

**Verification approach:** No test infrastructure exists in the repo. The merge gate is `npm run build` (which runs `tsc -b && vite build`) plus `npm run lint`, plus a manual dev-server visual smoke test in a browser at the standard 800×480 viewport. Each task ends with a static `npm run build` to catch type errors as soon as they appear.

---

## File Structure

**Create:**
- `src/lib/color.ts` — `RGB` interface, `rgbToCss`, `scale`, `contrastColor`. View-agnostic color helpers.
- `src/lib/math.ts` — `clamp01`. View-agnostic numeric helper.

**Modify:**
- `src/hooks/useLed.ts` — drop inline `RGB` interface, import it from `lib/color`. `BridgeStatus` stays as the canonical export.
- `src/constants/lightPresets.ts` — switch `RGB` import from `useLed` to `lib/color`.
- `src/components/LightsView.tsx` — drop inline `rgbToCss`/`scale`/`contrastColor`/RGB import; import from `lib/color`. Drop inline `'unknown' | 'ok' | 'error'` union in `Header`; import `BridgeStatus` from `useLed`.
- `src/components/DriveView.tsx` — drop inline `clamp01`; import from `lib/math`.
- `src/constants/views.ts` — `ViewDef` gains a `render` field of type `(props: ViewProps) => ReactElement`. `VIEWS` entries each carry their own renderer.
- `src/components/DashboardLayout.tsx` — drop the local `renderView` helper; inline a `VIEWS.find(...)!.render(...)` lookup.
- `package.json` — `"name"` from `"temp-app"` to `"gokart-dash"`.

**Delete:** None.

---

## Task 1: Create `src/lib/color.ts` and `src/lib/math.ts`

Two tiny new utility modules. Bundled into one commit because they have no inter-dependencies and are both new files.

**Files:**
- Create: `src/lib/color.ts`
- Create: `src/lib/math.ts`

- [ ] **Step 1: Create `src/lib/color.ts`**

Write this exact content:

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

- [ ] **Step 2: Create `src/lib/math.ts`**

Write this exact content:

```ts
export function clamp01(n: number): number {
  return Math.min(1, Math.max(0, n));
}
```

- [ ] **Step 3: Verify both files compile**

Run: `npm run build`
Expected: exit 0. The new files have no consumers yet, so they compile in isolation.

- [ ] **Step 4: Commit**

```bash
git add src/lib/color.ts src/lib/math.ts
git commit -m "feat(dash): add src/lib/{color,math} shared helpers

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Migrate `RGB` ownership from `src/hooks/useLed.ts` to `src/lib/color.ts`

The new `lib/color.ts` already defines `RGB` (Task 1). Update `useLed.ts` to import it instead of redefining. `BridgeStatus` stays here as the canonical export.

**Files:**
- Modify: `src/hooks/useLed.ts`

- [ ] **Step 1: Read `src/hooks/useLed.ts`** to confirm the current import section and `RGB` definition.

- [ ] **Step 2: Replace the imports + `RGB` interface block**

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

The `export type { RGB }` re-export keeps backwards compatibility for any consumer still doing `import { type RGB } from '...useLed'` until they migrate. Tasks 3 and 4 update the only two such consumers, but the re-export costs nothing and prevents an accidental break.

- [ ] **Step 3: Verify the file still compiles**

Run: `npm run build`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add src/hooks/useLed.ts
git commit -m "refactor(dash): import RGB from lib/color in useLed

The RGB type now lives in lib/color (where the helpers that operate on
it live). useLed re-exports it for backwards compatibility.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Switch `src/constants/lightPresets.ts` to import `RGB` from `lib/color`

This file imports `RGB` from `useLed` today, which is a weird dependency direction (constants importing from a hook). Point it at `lib/color`.

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

Run: `npm run build`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add src/constants/lightPresets.ts
git commit -m "refactor(dash): import RGB from lib/color in lightPresets

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Update `src/components/DriveView.tsx` to import `clamp01` from `lib/math`

Drop the local `clamp01` definition; import from the shared module.

**Files:**
- Modify: `src/components/DriveView.tsx`

- [ ] **Step 1: Add the import**

Find this block at the top of the file (around lines 1–5):

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

Find and delete this function at the bottom of the file (currently lines 159–161):

```tsx
function clamp01(n: number) {
  return Math.min(1, Math.max(0, n));
}
```

- [ ] **Step 3: Verify**

Run: `npm run build`
Expected: exit 0. The 4 call sites of `clamp01` (`rpmPct`, `redlinePct`, `motorTempPct`, and the `PedalBar`'s `clamp01(pct / 100)`) now resolve to the imported function.

- [ ] **Step 4: Commit**

```bash
git add src/components/DriveView.tsx
git commit -m "refactor(dash): use lib/math.clamp01 in DriveView

Drops the local clamp01 helper; the imported version is identical.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Update `src/components/LightsView.tsx` to use `lib/color` helpers and drop the inline `BridgeStatus` redefinition

Three small changes here, all related: use the shared color helpers, the shared `RGB` type, and the canonical `BridgeStatus` type.

**Files:**
- Modify: `src/components/LightsView.tsx`

- [ ] **Step 1: Replace the import block + remove inline color helpers**

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

What's removed:
- The local `rgbToCss` and `scale` definitions.
- The `type RGB` import (no longer needed in this file — `scale`'s parameter is typed inside `lib/color`).

What's added:
- `import { contrastColor, rgbToCss, scale } from '../lib/color';`
- `import { useLed, type BridgeStatus } from '../hooks/useLed';` (was `import { useLed, type RGB } from ...`).

- [ ] **Step 2: Delete the local `contrastColor` definition**

Find and delete (currently lines 144–148, after the spec change above the line numbers shift):

```tsx
function contrastColor({ r, g, b }: RGB): string {
  // Perceived luminance — white text on dark colors, black on light.
  const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  return lum > 0.6 ? '#0a0a0a' : '#ffffff';
}
```

(Identical content already exists in `lib/color.ts`; the imported version replaces it.)

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

Run: `npm run build`
Expected: exit 0. All call sites of `rgbToCss`, `scale`, and `contrastColor` now resolve to the imported versions; `Header`'s `status` prop now uses the imported `BridgeStatus` type.

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

## Task 6: Add `render` field to `ViewDef` in `src/constants/views.ts`

`VIEWS` becomes a true registry by storing each view's renderer. This task ALSO requires Task 7 to land in the same commit because `DashboardLayout`'s old `renderView` is no longer needed once `VIEWS` carries renderers — leaving it as dead code between tasks would compile-warn.

Actually, that's fine: TypeScript doesn't warn on unused code. The intermediate state (registry has render fields, DashboardLayout still uses the old `renderView`) compiles cleanly because the old code is still valid. Tasks 6 and 7 are sequential commits.

**Files:**
- Modify: `src/constants/views.ts`

- [ ] **Step 1: Read the current file** to know the exact existing content.

- [ ] **Step 2: Replace the entire file**

Overwrite `src/constants/views.ts` with this exact content:

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

**Important — rename the file to `.tsx`:** The original was `.ts`, but the new content includes JSX (the `render` arrow functions return `<DriveView ... />` etc.). `tsc -b` rejects JSX in a `.ts` file, so the file MUST be renamed to `.tsx`.

Use `git mv` so git tracks it as a rename rather than a delete + add:

```bash
git mv src/constants/views.ts src/constants/views.tsx
```

Then write the content above to the new path `src/constants/views.tsx`.

The two consumers of this module (`src/components/DashboardLayout.tsx` and `src/components/BottomDock.tsx`) import from `'../constants/views'` (no extension) — that resolves to `views.tsx` automatically once `views.ts` is gone. No consumer edits needed.

- [ ] **Step 3: Verify**

Run: `npm run build`
Expected: exit 0. Both `tsc -b` and `vite build` accept the new `.tsx` file. The old `renderView` in `DashboardLayout` still works (it doesn't read the new `render` field).

- [ ] **Step 4: Commit**

```bash
git add src/constants/views.tsx
# git mv has already staged the deletion of views.ts
git commit -m "refactor(dash): make VIEWS a renderer-carrying registry

Each entry now carries its own render fn keyed on ViewId. Renames
views.ts -> views.tsx since render fns return JSX. DashboardLayout
still uses the old renderView helper; the next commit removes it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Replace `renderView` in `src/components/DashboardLayout.tsx` with a registry lookup

With Task 6's `render` field in place, `DashboardLayout`'s `renderView` is now redundant. Replace it with a one-line lookup.

**Files:**
- Modify: `src/components/DashboardLayout.tsx`

- [ ] **Step 1: Replace the entire file**

Overwrite `src/components/DashboardLayout.tsx` with this exact content:

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
- Imports of `DriveView`, `LightsView`, `Placeholder` (now imported by `views.tsx` only).
- The local `renderView` helper at the bottom.

What's added:
- One line: `const view = VIEWS.find((v) => v.id === activeView)!;`

The `VIEWS.find(...)!` non-null assertion is safe because `activeView: ViewId` is a union literal type and `VIEWS` is exhaustive over that union.

- [ ] **Step 2: Verify**

Run: `npm run build`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add src/components/DashboardLayout.tsx
git commit -m "refactor(dash): replace renderView if/else with VIEWS registry lookup

Adding a real view is now a single edit in views.tsx — replace the
Placeholder render fn with the actual component. DashboardLayout no
longer needs to know about specific view types.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Fix `package.json` `name`

Cosmetic, but the Vite scaffold's `"temp-app"` has been here since project init.

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

Run: `npm run build`
Expected: exit 0. `package-lock.json` may also pick up a `"name"` change at its top level; if so, stage it too. Run `npm install --package-lock-only` if needed to refresh.

- [ ] **Step 3: Commit**

```bash
git add package.json package-lock.json
git commit -m "chore(dash): rename package from temp-app to gokart-dash

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Final verification — static gates + dev-server visual smoke

This is the merge gate. Three checks: static build, lint, manual visual smoke in a browser.

**Files:**
- Read-only verification.

- [ ] **Step 1: Static build**

```bash
npm run build
```

Expected: exit 0. The build artifact lands in `dist/`.

- [ ] **Step 2: Lint**

```bash
npm run lint
```

Expected: exit 0. (If the project has had pre-existing lint warnings, capture them in the PR description as out-of-scope.)

- [ ] **Step 3: Dev-server smoke**

Start the dev server in the background:

```bash
npm run dev
```

This binds to `http://localhost:5173`. The server logs the actual URL — note it.

Then open the URL in a browser at the standard kiosk viewport (800×480). The Lights view will say "Bridge offline" — that's expected since no Pi is running. Verify:

- [ ] **Drive view (default)** — animates: speed/RPM/throttle move; gear flips between N and D; motor temp drifts; battery + range pills render.
- [ ] **Bottom dock** — five items: Drive, Map, Camera, Lights, System. Tapping each switches the main panel; transitions are smooth; the small underline indicator slides to the active tab.
- [ ] **Map / Camera / System** — show the Placeholder with the appropriate icon and label and "Coming soon".
- [ ] **Lights view** — header shows "Bridge offline" badge (red); 4×2 grid of preset swatches; tapping a preset toggles it active; brightness slider drag from 0–100%; power toggle off → all swatches dim, on → swatches use current brightness.
- [ ] **Status bar** — current time displayed; mode label ("SAFE" / "TURBO" / "2FST2BVR") shown; armed/disarmed pill; GPS sat count; battery percent.

If anything looks different from pre-refactor (compare against your memory or a captured screenshot), STOP and investigate.

- [ ] **Step 4: Stop the dev server**

Kill the background dev-server process (Ctrl+C in its terminal, or `kill` the pid).

- [ ] **Step 5: No commit needed for verification.**

If all three checks pass, the React/TS sub-project is done. Hand back to the controller for the final code review and PR creation.

---

## Done criteria

- All 9 tasks above have their checkboxes ticked.
- `npm run build` exits 0.
- `npm run lint` exits 0.
- Dev-server visual smoke passes for all 5 views and the status bar / bottom dock.
- `git log main..HEAD --stat` shows: 2 new files in `src/lib/`, modifications across `src/components/`, `src/constants/`, `src/hooks/`, plus the package.json / package-lock.json change. No changes to `hardware-scripts/`, `deploy/`, or any file outside `src/` and `package.json` (other than this plan and the spec doc).

When all of the above hold, the React/TS sub-project is complete. The next sub-project is the Teensy firmware refactor — its own spec, plan, branch, and PR.
