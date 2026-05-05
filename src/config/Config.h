#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace smartrc {

struct Config {
    // Home Wi-Fi (STA)
    String   wifiSsid;
    String   wifiPassword;

    // Fallback hotspot (AP)
    String   apSsid;
    String   apPassword;   // empty = open network

    // mDNS / DHCP hostname
    String   hostname;

    // IP configuration
    bool      useDhcp = true;
    IPAddress staticIp{0, 0, 0, 0};
    IPAddress staticGateway{0, 0, 0, 0};
    IPAddress staticSubnet{255, 255, 255, 0};
    IPAddress staticDns{0, 0, 0, 0};

    // Motor / steering tuning.
    //   pulseMs   = upper bound on how long a single held press energises
    //               the motor. For hold-and-release UIs, the portal's JS
    //               re-pings faster than this so the motor stays on
    //               continuously; on release an explicit `steer_stop`
    //               command cuts power immediately (pulseMs is only the
    //               fallback timeout if a release packet is lost).
    //   cooldownMs = informational post-pulse coast window; does NOT gate
    //                user input.
    uint16_t steeringPulseMs    = 600;  // open-loop max hold window
    uint16_t steeringCooldownMs = 120;  // post-pulse coast (informational)
    uint8_t  defaultDrivePwm    = 200;  // 0..255
    uint8_t  defaultSteerPwm    = 220;  // 0..255

    // Active-brake: reverse-drives the motor when `brake` is issued
    // while the car is moving, using the IMU to detect when the vehicle
    // has actually come to rest. See Drive::brake() / Drive::update().
    uint8_t  activeBrakePwm     = 220;  // 0..255, how hard to reverse
    uint16_t activeBrakeMaxMs   = 600;  // hard upper bound on brake time

    // Auto-brake (obstacle-distance based forward-collision avoidance).
    // When enabled and the vehicle is moving forward faster than
    // autoBrakeMinSpeedCmPs, the front VL53L0X reading is compared to a
    // speed-dependent trigger distance:
    //     trigger_cm = autoBrakeBaseCm + autoBrakeSlopeCmPerMs * vx (m/s)
    // If the obstacle is closer than that, AutoBrake calls Drive::brake()
    // and CommandHandler rejects further `forward` commands until the
    // vehicle stops or the obstacle clears. Reverse + steering remain
    // available so the driver can back away. Does NOT latch — it's a
    // continuous evaluation, not an e-stop.
    bool     autoBrakeEnabled       = false;
    uint16_t autoBrakeBaseCm        = 20;   // trigger distance at 0 m/s
    uint16_t autoBrakeSlopeCmPerMs  = 30;   // extra cm per (m/s) of vx
    uint16_t autoBrakeMinSpeedCmPs  = 10;   // ignore below 0.1 m/s

    // Swap electrical polarity without rewiring. Leaves the logical API
    // unchanged — "forward" still means forward from the driver's POV.
    bool     driveInverted      = false;
    bool     steerInverted      = false;

    // IMU mount-orientation fix. Flip a given axis' sign (applied to both
    // accel and gyro for that axis) so the firmware reports values
    // relative to the vehicle's frame regardless of how the MPU6050
    // breakout physically sits on the chassis.
    //   X = forward/back   Y = left/right   Z = up/down
    bool     imuInvertX         = false;
    bool     imuInvertY         = false;
    bool     imuInvertZ         = false;

    // Controls whether the AP hotspot is torn down after a successful
    // Wi-Fi provision. true  -> AP closes once STA is verified (default).
    //                    false -> AP stays up alongside STA (AP_STA mode)
    //                    so the laptop/phone that provisioned stays on it.
    bool     apShutdownAfterProvision = true;

    // Optional shared-secret token. When non-empty, WebSocket `cmd` frames
    // must carry this in the `hello` handshake (`{t:"hello", token:"..."}`)
    // before commands are accepted. Empty (default) = auth disabled, fine
    // for a home LAN.
    String   controlToken;

    // Verbose log verbosity for the serial CLI / debug channel.
    // 0=off, 1=error, 2=warn, 3=info (default), 4=debug.
    uint8_t  logLevel = 3;

    // Safety
    //   Dead-man watchdog. If no accepted command arrives within this
    //   window, motors are force-stopped. Keep high enough to tolerate
    //   Wi-Fi jitter + mobile browser setInterval drift (real-world
    //   gaps of 300-900 ms happen on congested APs), but low enough
    //   that a genuinely-disconnected client stops the vehicle quickly.
    uint16_t heartbeatTimeoutMs = 1500;
};

// Load config from NVS, applying built-in defaults for missing keys.
void loadConfig(Config& out);

// Persist config to NVS. Returns true on success.
bool saveConfig(const Config& cfg);

// Wipe everything (Wi-Fi, AP, IP, motor tuning) and restore defaults.
void factoryReset();

// Wipe only Wi-Fi STA + IP settings (keeps motor tuning + AP creds).
void networkReset();

}  // namespace smartrc
