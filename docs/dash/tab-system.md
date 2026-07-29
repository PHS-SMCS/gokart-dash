# System Tab

The System tab is the dashboard's **diagnostics and simulator** panel. It is where
you choose the telemetry source, inspect the live link, test the notification
system, adjust display behavior, and — crucially — **drive the whole UI from a
simulator** with no hardware attached. It is organized as a grid of labeled
sections.

## Telemetry Source

The most important control on the tab. A segmented switch selects where the
dashboard's data comes from:

| Option | Meaning |
|---|---|
| **Off** | No source; widgets show a disconnected state. |
| **Live** | The real 20 Hz telemetry from the Teensy over the [bridge WebSocket](telemetry.md). |
| **Sim** | The built-in simulator, driven by the sliders below. |

A badge in the section header shows the link state — the **update rate in Hz** when
connected, or **idle** otherwise. Below it, a live diagnostics grid reports:

| Field | Meaning |
|---|---|
| **Source** | The active source. |
| **Rate** | Frames per second actually arriving. |
| **Frame age** | Milliseconds since the last frame (a stalled link shows this climbing). |
| **Seq** | The telemetry sequence counter. |
| **Uptime** | The Teensy's uptime, in seconds. |
| **State** | The current drive state. |

When **Live** is selected, the exact WebSocket URL the dashboard is using is shown
(useful for confirming it points at the right host/port).

## Notifications

The dashboard raises toast notifications automatically from telemetry
**transitions** (e.g. entering FAULT, a wheel disconnect, the contactor closing).
This section lets you **fire a test toast** at each severity level to verify the
notification system renders correctly:

- **Info** (e.g. "Precharging bus…")
- **Success** (e.g. "Contactor closed")
- **Warning** (e.g. "Wheel disconnected")
- **Danger** (e.g. a fault — sticky, no auto-dismiss)

These are test-only; real events come from the live telemetry stream.

## Display

- **Hide cursor (kiosk)** — on the kart the dashboard runs fullscreen with no
  pointer. Turn this off to show the mouse cursor while debugging on a workstation.
  The setting is **saved on the device** (localStorage), so the kiosk stays
  cursor-free across reloads.

## Simulator

When the source is set to **Sim**, the remaining sections become live controls that
feed the entire dashboard — every dial, bar, badge, and the map — from hand-set
values. This is how the UI is demoed and developed with no kart present. (When the
source is not Sim, these controls are shown but disabled.)

### Simulate · Drive

- **Drive state** — SAFE / ARMED / DRIVE / STOPPING / FAULT.
- **Gear** — Low / Med / High.
- **Speed** (0–40 mph), **Throttle** (0–100 %), **Brake** (0–100 %).
- **Fault code** (0–8) and a **Reverse** toggle.

### Simulate · Systems

- **Contactor** phase — Open / Precharge / Closed / Fault.
- **Subsystem health toggles** — Wheel, Steer link, Steer cal, ESC link, Bench.
- **Steer set** (−30…+30°), **Motor temp** (0–140 °C), **Batt volts** (60–90 V),
  **Batt amps** (0–300 A).

### Simulate · Position

- **GPS fix** toggle, **Heading** (0–359°), **Sats** (0–20), and **Lat/Lon** number
  fields — exercises the [Map tab](tab-map.md) and mini-map.
- A **Reset** button restores the simulator defaults.

!!! tip "Use Sim to explore every feature safely"
    Because the simulator drives the exact same widgets as live telemetry, it is
    the safest way to see what each drive state, fault code, contactor phase, and
    subsystem-health combination looks like on the dashboard — no kart, no risk.
