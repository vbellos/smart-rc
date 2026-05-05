#include <Arduino.h>
#include <ESPmDNS.h>
#include <Wire.h>

#include "Pins.h"
#include "config/Config.h"
#include "control/AutoBrake.h"
#include "control/CommandHandler.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/MotorDriver.h"
#include "motors/Steering.h"
#include "network/NetworkManager.h"
#include "portal/Portal.h"
#include "sensors/Sensors.h"
#include "transport/SerialCli.h"
#include "transport/WsServer.h"

namespace {
smartrc::Config         g_cfg;
smartrc::MotorDriver    g_driver;
smartrc::Drive          g_drive;
smartrc::Steering       g_steering;
smartrc::Safety         g_safety;
smartrc::AutoBrake      g_autoBrake;
smartrc::CommandHandler g_commands;
smartrc::NetworkManager g_net;
smartrc::Portal         g_portal;
smartrc::WsServer       g_ws;
smartrc::SerialCli      g_cli;
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

    // 4. Safety + command pipeline. AutoBrake is wired AFTER sensors come
    //    up (step 5) since it needs the IMU + distance pointers, but the
    //    CommandHandler captures the pointer now so it can gate forward
    //    commands once AutoBrake is armed.
    g_safety.begin(&g_drive, &g_steering, g_cfg.heartbeatTimeoutMs);
    g_commands.begin(&g_drive, &g_steering, &g_safety, &g_autoBrake);

    // 5. I²C bus + sensors. Wire.begin() must run before sensors::begin()
    //    because the MPU6050 driver talks over it during its probe.
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, pins::I2C_FREQ_HZ);
    smartrc::sensors::begin();
    smartrc::sensors::setImuInverts(
        g_cfg.imuInvertX, g_cfg.imuInvertY, g_cfg.imuInvertZ);

    // Drive's active-brake wants live IMU samples to detect "stopped".
    g_drive.setImu(&smartrc::sensors::imu());
    g_drive.setActiveBrakePwm(g_cfg.activeBrakePwm);
    g_drive.setActiveBrakeMaxMs(g_cfg.activeBrakeMaxMs);

    // AutoBrake — front-distance obstacle avoidance. Off by default; the
    // user enables + tunes it from the portal.
    g_autoBrake.begin(&g_drive,
                      &smartrc::sensors::distance(),
                      &smartrc::sensors::imu());
    g_autoBrake.setEnabled(g_cfg.autoBrakeEnabled);
    g_autoBrake.setFrontParams(g_cfg.autoBrakeFrontBaseCm,
                               g_cfg.autoBrakeFrontSlopeCmPerMs,
                               g_cfg.autoBrakeFrontMinSpeedCmPs);
    g_autoBrake.setRearParams (g_cfg.autoBrakeRearBaseCm,
                               g_cfg.autoBrakeRearSlopeCmPerMs,
                               g_cfg.autoBrakeRearMinSpeedCmPs);

    // 6. Networking — STA with AP fallback.
    g_net.begin(g_cfg);

    // 7. Web portal + JSON API.
    g_portal.begin({
        .config    = &g_cfg,
        .network   = &g_net,
        .commands  = &g_commands,
        .drive     = &g_drive,
        .steering  = &g_steering,
        .safety    = &g_safety,
        .autoBrake = &g_autoBrake,
    });

    // 8. WebSocket transport on the SAME :80 listener (Phase 2+).
    g_ws.begin(g_portal.server(), {
        .config    = &g_cfg,
        .commands  = &g_commands,
        .drive     = &g_drive,
        .steering  = &g_steering,
        .safety    = &g_safety,
        .network   = &g_net,
        .autoBrake = &g_autoBrake,
    });

    // 9. mDNS so mobile/web clients can find us at `<hostname>.local`
    //    without chasing DHCP leases. TXT records advertise the proto
    //    version and the WS path.
    if (MDNS.begin(g_cfg.hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "proto", "1");
        MDNS.addServiceTxt("http", "tcp", "ws",    "/ws");
        MDNS.addServiceTxt("http", "tcp", "device","smartrc-esp32");
        Serial.printf("[mdns] advertising %s.local on :80\n",
                      g_cfg.hostname.c_str());
    } else {
        Serial.println("[mdns] begin failed");
    }

    // 10. Serial CLI (USB) — bench-testing, debugging, Wi-Fi recovery
    //     when the portal is unreachable. Non-blocking, lives in loop().
    g_cli.begin({
        .config   = &g_cfg,
        .commands = &g_commands,
        .drive    = &g_drive,
        .steering = &g_steering,
        .safety   = &g_safety,
        .network  = &g_net,
    });

    Serial.println("[boot] ready");
}

void loop() {
    g_net.update();
    g_portal.handle();     // no-op since async migration
    g_ws.update();         // telemetry push + event polling
    g_cli.update();        // serial console
    smartrc::sensors::update();   // pump IMU + distance before others read
    g_autoBrake.update();         // evaluate obstacle gate; may issue brake
    g_drive.update();             // active-brake tick
    g_steering.update();
    g_safety.update();
}
