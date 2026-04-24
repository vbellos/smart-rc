# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32-S3 firmware (PlatformIO + Arduino framework) for a small RC platform
driven by a **TB6612FNG** dual H-bridge. Target board:
**4D Systems gen4-ESP32-S3 R8N16** (8 MB octal PSRAM, 16 MB flash).

- **Motor B (channel B)** — rear DC motor, forward/reverse/stop.
- **Motor A (channel A)** — front DC motor used as a steering actuator.
  Plain DC motor — *not* a servo, no position feedback. Steering is
  **open-loop timed pulses** with a reversal cooldown, never angle control.

Pin map is fixed per project spec and lives in `src/Pins.h`. TB6612 `STBY`
and `VCC` are wired to 3V3; if you ever want firmware-controlled standby,
free a GPIO and add it to `Pins.h` + `MotorDriver::begin()` — don't try to
fake it via the existing pins.

## Common commands

```sh
pio run                              # build firmware (default env: ESP32-S3)
pio run -t upload                    # flash device
pio device monitor                   # serial console (115200)
pio run -t upload -t monitor         # build + flash + monitor
pio run -t clean                     # wipe .pio build cache

pio test -e native_test              # host-side Unity tests for Steering FSM
pio test                             # device tests (excludes test_steering_native)
```

The native test env uses a stub `Arduino.h` under `test/native_stubs/` and
only compiles `motors/MotorDriver.cpp` + `motors/Steering.cpp` (see
`build_src_filter` in `platformio.ini`). To make another module
host-testable, add its `.cpp` to that filter — and only if it has no
hardware-dependent includes.

## Architecture

Layered, single-direction dependencies — main.cpp owns every module and
wires them together once at boot:

```
main.cpp
  ├─ Config (NVS via Preferences)              src/config/
  ├─ MotorDriver (TB6612 + LEDC PWM)           src/motors/
  │    └─ Drive  (rear, channel B)             src/motors/
  │    └─ Steering (front, channel A, FSM)     src/motors/
  ├─ Safety (heartbeat watchdog + e-stop)      src/control/
  ├─ CommandHandler (single command funnel)    src/control/
  ├─ NetworkManager (STA → AP fallback, mDNS)  src/network/
  ├─ Portal    (HTTP + JSON API + HTML)        src/portal/
  ├─ WsServer  (WebSocket realtime/telemetry)  src/transport/
  ├─ SerialCli (USB serial debug console)     src/transport/
  └─ Sensors (intentional empty stub)          src/sensors/

Transports (Portal / WsServer / SerialCli) are peers: each is a thin
adapter that calls CommandHandler::execute() — nothing else touches the
motor classes directly.
```

Key invariants — preserve when modifying:

- **Boot order in `main.cpp::setup()` matters.** `MotorDriver::begin()` +
  `stopAll()` runs *before* anything else — motors must be in a known-
  stopped state before config/network code can touch them.
- **`MotorDriver` is policy-free.** It only takes `(channel, dir, speed)`.
  All "this is steering" / "this is drive" semantics live in `Drive` and
  `Steering`. Don't push policy back down into `MotorDriver`.
- **`Steering` is non-blocking.** It exposes `pulseLeft/pulseRight` that
  return `bool` (false = rejected: pulse already running in opposite
  direction, or reverse-direction cooldown not elapsed). `update()` must
  be called every loop() to end pulses + clear cooldown.
- **`CommandHandler::execute()` is the single chokepoint.** Every accepted
  command must flow through it so `Safety::notifyHeartbeat()` is called
  exactly once per command. New transports (WebSocket, BLE, ESP-NOW…)
  should call `execute()`, not the motor classes directly.
- **Safety latch beats everything.** When `Safety::isEmergency()` is true,
  `CommandHandler` rejects all commands except `clear_estop`.

## Config & persistence

`Config` is a plain struct (`src/config/Config.h`). Persistence uses
`Preferences` (NVS) under namespace **`smartrc`**. Keys are short
(`wifi_ssid`, `st_ms`, …) — see `Config.cpp`.

To add a new persisted setting:
1. Add the field to `struct Config`.
2. Add load/save lines in `Config.cpp` (mirror the existing pairs).
3. Add it to the JSON in `Portal::handleGetConfig()` and the parsing in
   `handlePostConfig()` — it will round-trip through the portal automatically.
4. If it tunes a live subsystem (motor, safety), apply it in
   `handlePostConfig()` after `saveConfig()` so the change takes effect
   without reboot.

`factoryReset()` wipes the entire namespace; `networkReset()` only clears
Wi-Fi/IP keys (handy when motor tuning is fine but Wi-Fi creds are wrong).

## Networking

`NetworkManager::begin()` tries STA with a 12 s timeout, then falls back
to AP. The default AP creds are `SmartRC-Setup` / `smart1234` — defined
in `Config.cpp`, *not* hardcoded in the network manager. Reconnect logic
runs from `update()` on a 15 s interval when STA drops; AP mode does no
reconnect bookkeeping.

The web server is **`ESPAsyncWebServer`** (via `mathieucarbou/ESPAsyncWebServer`
+ `AsyncTCP`). The server runs on its own FreeRTOS task, so route handlers
do **not** block `loop()` — `Portal::handle()` is a no-op, retained only
for main.cpp API stability.

JSON-body POSTs go through `AsyncCallbackJsonWebHandler` which buffers the
chunked body and delivers a parsed `JsonVariant` to the handler — no
`server.arg("plain")` dance. Responses use `AsyncResponseStream` to
serialize straight into the TCP buffer without an intermediate `String`.

Two handlers still block their server task for seconds at a time by design:
`handleWifiScan` (~2–5 s) and `handleWifiProvision` (up to 12 s). They
block the HTTP task only, not the motor/safety loop. A future phase can
replace these with the async scan + a poll endpoint if concurrent HTTP
during provisioning becomes important.

**Thread-safety note:** handlers now run in the AsyncTCP task while
`Drive`, `Steering`, `Safety` are also touched from `loop()`. All the
shared fields are primitive (bools, enums, uint16_t) — torn reads are
possible but benign. Do NOT add complex mutable state to these classes
without adding explicit synchronisation (FreeRTOS mutex or a command
queue). `CommandHandler::execute()` remains the single chokepoint for
motor actions, so any new transport (WS, BLE, ESP-NOW) inherits the
same guarantees.

## Sensors extension point

`src/sensors/` is intentionally empty — `begin()` and `update()` are no-ops.
The convention sketched in `Sensors.h` is:
- own all sensor objects inside the `smartrc::sensors` namespace,
- expose `appendStatusJson(JsonObject&)` and call it from
  `Portal::handleStatus()` so readings appear in `/api/status` without
  changing the portal's structure.

Don't add sensor logic into `main.cpp`, `Drive`, or `Steering`.

## Layout

```
src/
  main.cpp           // boot orchestration only
  Pins.h             // fixed GPIOs + PWM constants
  config/   Config.{h,cpp}
  control/  CommandHandler.{h,cpp}, Safety.{h,cpp}
  motors/   MotorDriver.{h,cpp}, Drive.{h,cpp}, Steering.{h,cpp}
  network/  NetworkManager.{h,cpp}
  portal/   Portal.{h,cpp}, PortalHtml.h    // HTML kept in its own header
  sensors/  Sensors.{h,cpp}                  // empty extension point
test/
  native_stubs/Arduino.h                     // host stub for native tests
  test_steering_native/test_steering.cpp     // Unity tests
```

Build artifacts are in `.pio/` (gitignored). Includes are rooted at `src/`
via `-I src` in `build_flags`, so use `#include "motors/Drive.h"` style
across the project.
