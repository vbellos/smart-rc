# Smart RC — Controller App

A modern React + TypeScript + Vite + Tailwind companion app for the
[Smart RC firmware](../README.md).

- **Discovery** — finds devices on your LAN (`smartrc.local` via mDNS +
  `192.168.4.1` when on the AP). Remembers recent devices.
- **Drive** — big press-and-hold pad with Pointer Events + Wake Lock so
  drifting cursors and dimming screens can't kill your throttle.
- **Monitor** — live telemetry stream (signal, drive/steer state, safety),
  ring-buffer event log, ready-made cards for IMU / distance / battery
  sensors (populated the moment you wire the firmware's `sensors::`
  extension point).
- **Config** — motor tuning, Wi-Fi scan + provision, log level, control
  token, factory/network reset — all round-tripped through `/api/config`.

## Stack

- **Vite** + **React 18** + **TypeScript** + **react-router-dom**
- **Tailwind v4** (via `@tailwindcss/vite`) — zero config JS, just
  `@import "tailwindcss"` in the single CSS entry.
- Zero runtime UI-kit dependencies: all icons are inline SVG, no animation
  library, no chart library (sparklines are hand-drawn SVG).

## Running

```sh
cd app
npm install
npm run dev           # → http://localhost:5173
```

Open the URL on any device on the same Wi-Fi as the RC. Because Vite's
`server.host` is enabled, your phone can load the dev server directly
at `http://<your-laptop-ip>:5173`.

```sh
npm run build         # production build to dist/
npm run preview       # serve the built dist over LAN
```

## How it talks to the device

- **REST** (`/api/status`, `/api/config`, `/api/wifi/*`, `/api/reboot`,
  `/api/reset/*`) for discovery + configuration + provisioning.
- **WebSocket** (`/ws`) for realtime control + telemetry stream. Commands
  fall back to HTTP if the socket isn't open.

The firmware exposes `Access-Control-Allow-Origin: *` on every `/api/*`
response so this app can be hosted anywhere (Netlify, GitHub Pages,
localhost) and still hit the device.

## Structure

```
app/src/
  main.tsx                 # ReactDOM entry
  App.tsx                  # Routes + provider
  index.css                # Tailwind import + app theme tokens
  lib/
    types.ts               # shared wire types (Config, Status, WS frames)
    api.ts                 # typed REST client
    ws.ts                  # reconnecting WebSocket client
    discovery.ts           # probe smartrc.local + 192.168.4.1
    storage.ts             # localStorage (recent devices, last host)
  context/
    DeviceContext.tsx      # connect/disconnect + api + ws + status
  hooks/
    useHold.ts             # Pointer-Events press-and-hold (no drift kill)
    useTelemetry.ts        # latest telemetry frame + event ring buffer
    useControl.ts          # send cmd — WS first, HTTP fallback
  components/
    Layout.tsx             # AppShell + Sidebar + PageHeader
    ControlPad.tsx         # the drive pad
    StatCard.tsx           # dashboard-style hero stat
    Icons.tsx              # inline SVG icon set
  pages/
    DiscoverPage.tsx       # landing — find/pick device
    DrivePage.tsx          # control pad + key stats
    MonitorPage.tsx        # telemetry + sensor dashboard
    ConfigPage.tsx         # tabs: motors / wifi / system
```

## Adding sensors later

The Monitor page already has cards for IMU, distance, and battery. They
render in "Not connected" state until the firmware starts populating a
`sensors` object in its telemetry / `/api/status`. Recommended shape:

```json
{
  "sensors": {
    "imu":      { "gx":0.1, "gy":0.0, "gz":0.2, "ax":0.0, "ay":9.8, "az":0.0 },
    "distance": { "front": 87.3 },
    "battery":  { "voltage": 7.42, "percent": 68 }
  }
}
```

Drop that into `sensors::appendStatusJson()` (firmware side, the
extension point is `src/sensors/`) and the cards light up automatically.
Add any extra key and a new card can be added here with ~20 lines.

## Notes on the press-and-hold

The drive pad uses Pointer Events + `setPointerCapture`, so:

- A wobbling finger or drifting cursor **cannot** kill the hold — the
  button owns the pointer until `pointerup` or `pointercancel`.
- Alt-tab, tab hide, or window blur all trigger a clean release.
- While any direction is held, the page requests a **Screen Wake Lock**
  so mobile browsers don't throttle setInterval and cause silent drops.

## Custom host for dev

To point the app at a specific device hostname without the discovery
flow, just visit `/` and use the **Connect manually** field. The app
remembers the last host and auto-reconnects on next launch.
