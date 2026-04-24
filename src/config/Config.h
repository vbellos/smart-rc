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
    uint16_t steeringPulseMs    = 400;  // open-loop max hold window
    uint16_t steeringCooldownMs = 120;  // post-pulse coast (informational)
    uint8_t  defaultDrivePwm    = 200;  // 0..255
    uint8_t  defaultSteerPwm    = 220;  // 0..255

    // Swap electrical polarity without rewiring. Leaves the logical API
    // unchanged — "forward" still means forward from the driver's POV.
    bool     driveInverted      = false;
    bool     steerInverted      = false;

    // Controls whether the AP hotspot is torn down after a successful
    // Wi-Fi provision. true  -> AP closes once STA is verified (default).
    //                    false -> AP stays up alongside STA (AP_STA mode)
    //                    so the laptop/phone that provisioned stays on it.
    bool     apShutdownAfterProvision = true;

    // Safety
    uint16_t heartbeatTimeoutMs = 800;  // motors stop if no command within
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
