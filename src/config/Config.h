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

    // -------------------------------------------------------------------
    // Stunts — per-stunt timing & PWM tunables. All live-applied on save.
    // -------------------------------------------------------------------

    // Spin (L/R): full-lock + full throttle, terminated by gyro once the
    // integrated |yaw| exceeds targetDeg. <360 undershoots, >360
    // overshoots (useful to compensate for motor run-on after cut).
    uint16_t stuntSpinTargetDeg   = 340;     // 90..720
    uint16_t stuntSpinTimeoutMs   = 2500;    // 500..10000
    uint8_t  stuntSpinPwm         = 255;

    // J-turn: fwd accel -> lock steer -> hard brake -> reverse w/ locked wheels.
    uint16_t stuntJturnFwdMs      = 700;
    uint16_t stuntJturnBrakeMs    = 300;
    uint16_t stuntJturnRevMs      = 900;
    uint8_t  stuntJturnPwm        = 255;

    // Wiggle: fwd kick + N full-swing L/R steering cycles (drive stays on).
    uint16_t stuntWiggleKickMs    = 150;
    uint16_t stuntWiggleHoldMs    = 280;     // per steer flick
    uint8_t  stuntWiggleCycles    = 3;       // 1..8 L-R pairs
    uint8_t  stuntWigglePwm       = 255;

    // Drift (L/R): entry fwd -> hard lock -> sustained throttle -> counter.
    uint16_t stuntDriftFwd1Ms     = 300;
    uint16_t stuntDriftLockMs     = 400;
    uint16_t stuntDriftCounterMs  = 300;
    uint8_t  stuntDriftPwm        = 255;

    // Power reverse: fwd -> brake -> reverse.
    uint16_t stuntPwrRevFwdMs     = 600;
    uint16_t stuntPwrRevBrakeMs   = 250;
    uint16_t stuntPwrRevRevMs     = 700;
    uint8_t  stuntPwrRevPwm       = 255;
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
