#include <Arduino.h>

#include "config/Config.h"
#include "control/CommandHandler.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/MotorDriver.h"
#include "motors/Steering.h"
#include "network/NetworkManager.h"
#include "portal/Portal.h"
#include "sensors/Sensors.h"

namespace {
smartrc::Config         g_cfg;
smartrc::MotorDriver    g_driver;
smartrc::Drive          g_drive;
smartrc::Steering       g_steering;
smartrc::Safety         g_safety;
smartrc::CommandHandler g_commands;
smartrc::NetworkManager g_net;
smartrc::Portal         g_portal;
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("\n[boot] Smart RC starting");

    // 1. Motors first — they MUST be in a known-stopped state ASAP.
    g_driver.begin();
    g_driver.stopAll();

    // 2. Persistent config.
    smartrc::loadConfig(g_cfg);

    // 3. High-level motor abstractions, fed from config.
    g_drive.begin(&g_driver, g_cfg.defaultDrivePwm);
    g_drive.setInverted(g_cfg.driveInverted);
    g_steering.begin(&g_driver,
                     g_cfg.steeringPulseMs,
                     g_cfg.steeringCooldownMs,
                     g_cfg.defaultSteerPwm);
    g_steering.setInverted(g_cfg.steerInverted);

    // 4. Safety + command pipeline.
    g_safety.begin(&g_drive, &g_steering, g_cfg.heartbeatTimeoutMs);
    g_commands.begin(&g_drive, &g_steering, &g_safety);

    // 5. Sensors (stub).
    smartrc::sensors::begin();

    // 6. Networking — STA with AP fallback.
    g_net.begin(g_cfg);

    // 7. Web portal + JSON API.
    g_portal.begin({
        .config   = &g_cfg,
        .network  = &g_net,
        .commands = &g_commands,
        .drive    = &g_drive,
        .steering = &g_steering,
        .safety   = &g_safety,
    });

    Serial.println("[boot] ready");
}

void loop() {
    g_net.update();
    g_portal.handle();
    g_steering.update();
    g_safety.update();
    smartrc::sensors::update();
}
