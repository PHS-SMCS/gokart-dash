# GoKart Dash

Touchscreen dashboard for the SMCS Robotics go-kart. Runs as a fullscreen
kiosk on a Raspberry Pi with a 7" DSI capacitive panel, talking to the
[SMCSKart mainboard](docs/SMCSKart-Mainboard/README.md) (Teensy 4.1 + ESC +
sensors) over UART.

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  Raspberry Pi 4  (Pi OS Lite, no desktop environment)            │
│                                                                  │
│   Cage (Wayland kiosk)                                           │
│      └── Chromium --kiosk                                        │
│             └── React SPA  ◀── HTTP ──┐                          │
│                                       │                          │
│   gokart-dash-web.service             │                          │
│      static server :5173 ─────────────┘                          │
│                                       ▲                          │
│   gokart-bridge.service               │ fetch /api/led, …        │
│      teensy_bridge.py :5174 ──────────┘                          │
│             │                                                    │
│             │ /dev/serial0  (UART, 115200, line protocol)        │
└─────────────┼────────────────────────────────────────────────────┘
              ▼
      Teensy 4.1  (kart_controller.ino)
        ├── ESC control (throttle DAC, speed/brake/reverse switches)
        ├── 24V RGB LED strip (PWM pins 33/36/37)
        ├── CAN bus, GPS PPS, hall pulses
        └── USB host (steering wheel)
```

Two long-running services on the Pi, both localhost-only:

| Service | Port | Purpose |
|---|---|---|
| `gokart-dash-web.service` | 5173 | Serves the built SPA out of `dist/` |
| `gokart-bridge.service`   | 5174 | Translates HTTP → Teensy serial |

## Repo layout

| Path | What lives here |
|---|---|
| `src/` | React + TypeScript dashboard (Vite) |
| `src/components/` | UI components — one per view (`DriveView`, `LightsView`, …) plus shared chrome (`StatusBar`, `BottomDock`) |
| `src/hooks/` | React hooks that wrap external state — `useTelemetry` (mock today, WebSocket later), `useLed` (POSTs to the bridge) |
| `src/constants/views.ts` | Registry of dashboard views shown in the bottom dock |
| `deploy/` | systemd units, udev rules, Cage launcher, kiosk install guide ([`deploy/README.md`](deploy/README.md)) |
| `hardware-scripts/raspberry-pi/` | Pi-side Python: hardware probes + the `teensy_bridge.py` HTTP service |
| `hardware-scripts/teensy-4.1/` | Teensy firmware (`kart_controller.ino`) — the authoritative command surface |
| `hardware-scripts/host/` | Operator CLI tools (`kartctl.py`, etc.) for bench bring-up |
| `docs/SMCSKart-Mainboard/` | Hardware reference: pin map, ESC connector, GPS/IMU |

## Quick start

### Develop on a workstation

```bash
npm install
npm run dev          # http://localhost:5173 with HMR
```

The Lights view will report "Bridge offline" since there's no Teensy; other
views still render fully (telemetry uses mock data).

### Deploy to the Pi

See [`deploy/README.md`](deploy/README.md) — the **From-zero install** section
walks through every command needed to bring up a fresh Pi OS Lite image.

## Extending

### Adding a new dashboard view

1. Add an entry to [`src/constants/views.ts`](src/constants/views.ts):
   ```ts
   { id: 'gps', label: 'GPS', icon: Map }
   ```
2. Create the component in `src/components/<Name>View.tsx`.
3. Wire it into the `renderView` switch in
   [`src/components/DashboardLayout.tsx`](src/components/DashboardLayout.tsx).
4. The bottom dock auto-includes it; no further changes.

The viewport is **800×480 fixed** (status bar 36, dock 56, content 388).
Design at this size — Chromium runs with `--force-device-scale-factor=1` so
1 CSS pixel = 1 panel pixel. Default font size is 16px; use Tailwind's
`tabular-nums` for any animated numeric value to prevent jitter.

### Adding a new Teensy-backed control

1. **Firmware**: confirm the command exists in
   [`kart_controller.ino`](hardware-scripts/teensy-4.1/kart_controller/kart_controller.ino).
   The firmware is the source of truth; do not invent commands the Teensy
   won't accept.
2. **Bridge**: add a branch in
   [`teensy_bridge.py`](hardware-scripts/raspberry-pi/teensy_bridge.py)
   under `Handler.do_GET` / `do_POST`. Call `self.link.send("…")` —
   thread-safe, auto-reopens on serial errors.
3. **UI**: write a hook similar to
   [`src/hooks/useLed.ts`](src/hooks/useLed.ts) — debounced `fetch()` with
   abort-and-replace so a slider drag never queues up requests.
4. Restart the bridge: `sudo systemctl restart gokart-bridge`.

### Replacing the mock telemetry

[`src/hooks/useTelemetry.ts`](src/hooks/useTelemetry.ts) currently fakes a
sine-wave speed/RPM/throttle so the UI animates during bring-up. To wire
real data, replace the `setInterval` body with a `WebSocket` (or
EventSource) subscription that produces the same `Telemetry` shape — no
component changes needed.

## Safety

The Teensy firmware enforces an **arm/disarm gate** on every output that can
move the kart (throttle, brake, reverse, contactor, speed mode). Lights,
status reads, and CAN polling are unrestricted. The dashboard intentionally
exposes only unrestricted operations today; any future propulsion controls
must surface the arm state and respect `ERR NOT_ARMED` responses.

See [`hardware-scripts/README.md`](hardware-scripts/README.md) for the
"Safety First" bring-up checklist before powering anything live.

## License

MIT.
