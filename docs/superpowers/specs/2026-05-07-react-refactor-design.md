# React/TS dashboard refactor — `src/`

**Sub-project 2 of 3** in the codebase refactor. Sibling specs cover the Python
`hardware-scripts/` consolidation (sub-project 1, already PR'd) and the Teensy
firmware (sub-project 3, to follow).

## Goal

Eliminate the hand-maintained `if/else` branch in `DashboardLayout.renderView`
by making `VIEWS` a true registry. Co-locate scattered local utility helpers.
Remove a small redundant type redeclaration. Fix the Vite scaffold leftover in
`package.json`.

No visual or behavioral change for end users.

## Why

Today `src/components/DashboardLayout.tsx` has a `renderView(id, telemetry)`
helper with a hand-coded `if/else` cascade:

```tsx
if (id === 'drive') return <DriveView telemetry={telemetry} />;
if (id === 'lights') return <LightsView />;
const def = VIEWS.find((v) => v.id === id)!;
return <Placeholder label={def.label} icon={def.icon} />;
```

Adding a real view requires editing two files (`views.ts` and the cascade) and
keeps growing the `if/else`. The README's "Adding a new dashboard view" section
even says so explicitly. The fix is registry-driven dispatch — each `VIEWS`
entry carries its own renderer.

Three other small issues compound:

- `clamp01` is defined locally in `DriveView.tsx`. It's a generic helper.
- `rgbToCss`, `scale`, `contrastColor` are all defined locally in
  `LightsView.tsx`. They operate on the `RGB` type and aren't view-specific.
- `LightsView` redeclares the union `'unknown' | 'ok' | 'error'` inline in its
  `Header` component instead of importing the existing `BridgeStatus` type from
  `useLed.ts`.
- `package.json` `"name"` is still `"temp-app"` from the original `npm create
  vite` scaffold.

## Out of scope

- Tests (Vitest / RTL infra does not exist; introducing it is its own project).
- New views or features. The `Placeholder` for `map`, `camera`, `system`
  remains; this PR only changes how it's wired up.
- Replacing the mock `useTelemetry` with a real WebSocket subscription. The
  README documents that as a separate task.
- Tailwind class cleanup, animation tuning, layout changes.
- Adding a barrel index (`src/components/index.ts`, etc.) — current import
  graph isn't large enough for that to reduce real friction.

## Architecture

### File structure

```
src/
├── lib/                          NEW — shared, view-agnostic helpers
│   ├── color.ts                  RGB type, rgbToCss, scale, contrastColor
│   └── math.ts                   clamp01
├── components/
│   ├── DashboardLayout.tsx       renderView shrinks to a registry lookup
│   ├── DriveView.tsx             import clamp01 from lib/math
│   ├── LightsView.tsx            import color helpers from lib/color, drop
│   │                             the inline BridgeStatus redefinition
│   ├── BottomDock.tsx            unchanged
│   ├── StatusBar.tsx             unchanged
│   └── Placeholder.tsx           unchanged
├── constants/
│   ├── views.ts                  ViewDef gains a `render` field; VIEWS holds
│   │                             the renderer for each id
│   ├── lightPresets.ts           import RGB from lib/color (was: from useLed)
│   └── motion.ts                 unchanged
├── hooks/
│   ├── useLed.ts                 RGB now lives in lib/color; useLed re-exports
│   │                             or imports it
│   └── useTelemetry.ts           unchanged
└── ...
```

### Registry-driven view dispatch

`src/constants/views.ts` evolves from a `{ id, label, icon }` list to a
`{ id, label, icon, render }` list. The render fn receives the props every view
might need (today: just `telemetry`):

```ts
import type { ReactElement } from 'react';
import type { Telemetry } from '../hooks/useTelemetry';

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
  { id: 'drive',  label: 'Drive',  icon: Gauge,     render: ({ telemetry }) => <DriveView telemetry={telemetry} /> },
  { id: 'map',    label: 'Map',    icon: Map,       render: () => <Placeholder label="Map" icon={Map} /> },
  { id: 'camera', label: 'Camera', icon: Camera,    render: () => <Placeholder label="Camera" icon={Camera} /> },
  { id: 'lights', label: 'Lights', icon: Lightbulb, render: () => <LightsView /> },
  { id: 'system', label: 'System', icon: Settings2, render: () => <Placeholder label="System" icon={Settings2} /> },
];
```

`DashboardLayout` then becomes:

```tsx
{VIEWS.find((v) => v.id === activeView)!.render({ telemetry })}
```

Adding a new real view becomes one edit: replace `Placeholder` with the actual
component in the corresponding entry.

**Trade-off:** `views.ts` now imports `DriveView`, `LightsView`, `Placeholder`,
and `Telemetry`. This is a one-way edge — those components don't import from
`views.ts` today (only `BottomDock` and `DashboardLayout` do, neither of which
is imported by view components). Verified clean: no circular dependency.

**Alternative considered:** keep `views.ts` as just metadata and put the
renderer mapping in a new `src/components/viewRegistry.ts`. Rejected because it
adds a third file to maintain in sync; the unified registry is simpler.

### Shared utility modules

`src/lib/color.ts` — owns the `RGB` interface (was in `useLed.ts`) plus the
three color helpers currently inlined in `LightsView.tsx`:

```ts
export interface RGB { r: number; g: number; b: number; }

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
  const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  return lum > 0.6 ? '#0a0a0a' : '#ffffff';
}
```

`src/lib/math.ts` — owns the single tiny helper currently inlined in
`DriveView.tsx`:

```ts
export function clamp01(n: number): number {
  return Math.min(1, Math.max(0, n));
}
```

The `RGB` move means `useLed.ts` and `lightPresets.ts` switch their `RGB`
import from `useLed` to `lib/color`. `useLed.ts` still exports
`type BridgeStatus`; `LightsView.Header` imports it from `useLed` instead of
redeclaring the union inline.

### `package.json` name

```diff
-  "name": "temp-app",
+  "name": "gokart-dash",
```

Cosmetic, but the scaffold artifact has been around since project init. The
deploy README and systemd unit don't reference the package name.

## Migration plan (per change)

1. Create `src/lib/color.ts` with `RGB`, `rgbToCss`, `scale`, `contrastColor`.
2. Create `src/lib/math.ts` with `clamp01`.
3. Update `src/hooks/useLed.ts` to import `RGB` from `lib/color` (drop the
   inline interface; keep `BridgeStatus` here as the canonical export).
4. Update `src/constants/lightPresets.ts` to import `RGB` from `lib/color`.
5. Update `src/components/LightsView.tsx`:
   - Import `rgbToCss`, `scale`, `contrastColor`, `RGB` from `lib/color`.
   - Drop the local `rgbToCss`, `scale`, `contrastColor` definitions.
   - Import `BridgeStatus` from `useLed` and use it instead of the inline
     union in the local `Header` component.
6. Update `src/components/DriveView.tsx`:
   - Import `clamp01` from `lib/math`.
   - Drop the local `clamp01`.
7. Update `src/constants/views.ts` to add the `render` field on `ViewDef` and
   populate it for each VIEWS entry. Imports of `DriveView`, `LightsView`,
   `Placeholder`, `Telemetry` are added here.
8. Update `src/components/DashboardLayout.tsx`:
   - Drop the local `renderView` helper.
   - Inline the registry lookup: `VIEWS.find((v) => v.id === activeView)!.render({ telemetry })`.
9. Edit `package.json` `name` to `"gokart-dash"`.

Each migration step is committable on its own; the file-move ones (`RGB`,
`clamp01`, color helpers) compile-pass at every intermediate state if done in
the listed order.

## Verification (no test infra → static + dev-server smoke)

The merge gate is "the dashboard renders identically and behaves identically
in a dev-server browser session."

- **Static:** `npm run build` exits 0 (this runs `tsc -b && vite build`,
  catching all TS errors).
- **Static:** `npm run lint` exits 0.
- **Type:** `tsc --noEmit` exits 0 (covered by `npm run build` but useful for
  faster local iteration).
- **Manual smoke (dev-server):** `npm run dev`, open `http://localhost:5173`:
  - Drive view animates (mock telemetry produces moving speed/RPM/throttle).
  - Bottom dock: tap each of the 5 views; transitions are smooth, status bar
    persists, mode label persists.
  - Lights view: pick each of the 8 presets — swatch ring moves, brightness
    slider drag works (0–100%), power toggle works (off → black, on →
    last-picked preset color).
  - Lights view header shows "Bridge offline" since no Pi/Teensy is running
    locally — that's expected pre-refactor behavior.
  - Status bar shows current time, mode, armed/disarmed, GPS sat count,
    battery percent.
- **Visual regression:** no automated tooling available; rely on manual
  inspection at the standard 800×480 viewport. Any deviation from the
  pre-refactor look is a regression.

## Risks and mitigations

- **R1: Circular import in views.ts.** `views.ts` now imports view components.
  Verified: no view component imports from `views.ts`. The static `npm run
  build` catches any cycle the IDE might hide.
- **R2: `VIEWS.find(...)!` non-null assertion.** Already present in pre-refactor
  code; the registry is exhaustive for `ViewId`. The refactor preserves the
  invariant. If a future drift adds a `ViewId` without an entry, the build
  passes but the bang fails at runtime — same as today.
- **R3: `RGB` import path change** could break a consumer not on the migration
  list. Mitigation: `git grep` for `from '.*useLed'` after step 3 — every
  consumer should import only `BridgeStatus`/`useLed` (not `RGB`) post-step.
  Update if any are missed.
- **R4: `package.json` name change** could affect `npm publish` or similar
  tooling. This isn't a published package; the `private: true` flag is set.
  Safe.

## Deliverables

- `src/lib/color.ts`, `src/lib/math.ts` created.
- `src/components/{DashboardLayout,DriveView,LightsView}.tsx` updated as
  described.
- `src/constants/{views,lightPresets}.ts` updated.
- `src/hooks/useLed.ts` updated.
- `package.json` `name` updated.
- No changes to Python, Teensy, deploy scripts, public assets, or any file
  outside `src/` and `package.json` (other than this spec doc and its
  follow-up plan).

## Sub-project context

This is **sub-project 2 of 3** in a multi-layer refactor. The Python
sub-project landed as a separate PR and does not affect any code under `src/`.
This sub-project does not affect any code under `hardware-scripts/` or
`hardware-scripts/teensy-4.1/`.
