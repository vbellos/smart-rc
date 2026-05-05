#include "transport/SerialCli.h"

#include <Wire.h>

#include "config/Config.h"
#include "control/CommandHandler.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"
#include "network/NetworkManager.h"
#include "sensors/Sensors.h"
#include "sensors/Mpu6050.h"

namespace smartrc {

namespace {

// Split a line into whitespace-separated tokens. Caps at kMaxTokens.
// In-place friendly: we return String copies (small strings) so the
// caller doesn't need to worry about lifetime.
constexpr int kMaxTokens = 6;

int tokenize(const String& line, String out[kMaxTokens]) {
    int n = 0;
    int i = 0;
    const int len = line.length();
    while (i < len && n < kMaxTokens) {
        while (i < len && isspace((unsigned char)line[i])) ++i;
        if (i >= len) break;
        int start = i;
        while (i < len && !isspace((unsigned char)line[i])) ++i;
        out[n++] = line.substring(start, i);
    }
    return n;
}

uint8_t parseSpeed(const String& s) {
    if (s.length() == 0) return 0;
    int v = s.toInt();
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

}  // namespace

void SerialCli::begin(const SerialCliDeps& deps) {
    deps_ = deps;
    buf_.reserve(kMaxLine);
    Serial.println();
    Serial.println("[cli] Serial CLI ready. Type 'help' for commands.");
    printPrompt();
}

void SerialCli::update() {
    // Background `imu watch` / `dist watch` streamer. Non-blocking — one
    // line per tick so motor/safety/net loops keep running while we log.
    if (watchKind_ != Watch::None) {
        const uint32_t now = millis();
        if ((int32_t)(now - watchEndMs_) >= 0) {
            watchKind_ = Watch::None;
            Serial.println("[watch] done");
            printPrompt();
        } else if ((int32_t)(now - watchNextMs_) >= 0) {
            if (watchKind_ == Watch::Imu) {
                const smartrc::sensors::Mpu6050& m = smartrc::sensors::imu();
                const auto& s = m.last();
                Serial.printf(
                    "%5lu  ax=%+6.2f  ay=%+6.2f  az=%+6.2f  vx=%+6.3f  stat=%d\n",
                    (unsigned long)(now - watchStartMs_),
                    s.ax, s.ay, s.az, m.velocityX(), (int)m.isStationary());
            } else {  // Watch::Dist
                auto& d = smartrc::sensors::distance();
                using Slot = smartrc::sensors::DistanceSensors::Slot;
                const bool fp = d.present(Slot::Front);
                const bool rp = d.present(Slot::Rear);
                const bool fv = d.valid(Slot::Front);
                const bool rv = d.valid(Slot::Rear);
                const uint16_t fmm = fp ? d.lastMm(Slot::Front) : 0;
                const uint16_t rmm = rp ? d.lastMm(Slot::Rear)  : 0;
                Serial.printf(
                    "%5lu  front=%4u mm (v=%d, p=%d)  rear=%4u mm (v=%d, p=%d)\n",
                    (unsigned long)(now - watchStartMs_),
                    fmm, (int)fv, (int)fp, rmm, (int)rv, (int)rp);
            }
            watchNextMs_ += 100;
        }
    }

    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (buf_.length() > 0) {
                Serial.println();  // echo newline
                processLine(buf_);
                buf_ = "";
            } else if (!promptShown_) {
                printPrompt();
            }
        } else if (c == 8 || c == 127) {  // backspace / del
            if (buf_.length() > 0) {
                buf_.remove(buf_.length() - 1);
                Serial.print("\b \b");  // visible erase on terminals
            }
        } else if (c >= 32 && c < 127) {
            if (buf_.length() < kMaxLine) {
                buf_ += c;
                Serial.print(c);   // local echo
                promptShown_ = false;
            }
        }
        // other control chars ignored
    }
}

void SerialCli::printPrompt() {
    Serial.print("smartrc> ");
    promptShown_ = true;
}

void SerialCli::printHelp() {
    Serial.println(
        "Commands:\n"
        "  help                       show this list\n"
        "  status                     print runtime status\n"
        "  config                     dump current config\n"
        "  fwd [speed]                drive forward (0-255)\n"
        "  rev [speed]                drive reverse\n"
        "  stop                       drive coast\n"
        "  brake                      drive short-brake\n"
        "  left [speed]               steer left pulse\n"
        "  right [speed]              steer right pulse\n"
        "  steer_stop                 release steering motor\n"
        "  estop                      latch emergency stop\n"
        "  clear                      clear emergency stop\n"
        "  net                        print network state\n"
        "  scan                       Wi-Fi scan\n"
        "  connect <ssid> <pass>      try creds, save on success\n"
        "  netreset                   wipe Wi-Fi + IP config\n"
        "  factory                    full NVS wipe & reboot\n"
        "  reboot                     soft reset\n"
        "  log <0..4>                 set log verbosity (persists)\n"
        "  token [<new> | clear]      show / set / clear controlToken\n"
        "  i2c scan                   probe I2C bus for devices\n"
        "  imu                        print current IMU reading\n"
        "  imu calibrate              re-sample gyro bias (hold still ~2s)\n"
        "  imu watch [sec]            stream ax/ay/az/vx every 100ms (default 6s)\n"
        "  dist                       print front/rear distance snapshot\n"
        "  dist watch [sec]           stream distance every 100ms (default 6s, max 60)\n");
}

void SerialCli::processLine(const String& line) {
    String tok[kMaxTokens];
    const int n = tokenize(line, tok);
    if (n == 0) { printPrompt(); return; }

    const String& cmd = tok[0];

    if      (cmd == "help")       printHelp();
    else if (cmd == "status")     cmdStatus();
    else if (cmd == "config")     cmdConfigShow();

    else if (cmd == "fwd")        cmdDrive("forward", tok, n);
    else if (cmd == "rev")        cmdDrive("reverse", tok, n);
    else if (cmd == "stop")       cmdDrive("stop",    tok, n);
    else if (cmd == "brake")      cmdDrive("brake",   tok, n);

    else if (cmd == "left")       cmdSteer("left",       tok, n);
    else if (cmd == "right")      cmdSteer("right",      tok, n);
    else if (cmd == "steer_stop") cmdSteer("steer_stop", tok, n);

    else if (cmd == "estop")      cmdEstop(true);
    else if (cmd == "clear")      cmdEstop(false);

    else if (cmd == "net")        cmdNet();
    else if (cmd == "scan")       cmdScan();
    else if (cmd == "connect")    cmdConnect(tok, n);
    else if (cmd == "netreset")   cmdNetReset();

    else if (cmd == "factory")    cmdFactory();
    else if (cmd == "reboot")     cmdReboot();

    else if (cmd == "log")        cmdLogLevel(tok, n);
    else if (cmd == "token")      cmdToken(tok, n);

    else if (cmd == "i2c" && n >= 2 && tok[1] == "scan") cmdI2cScan();
    else if (cmd == "imu" && n >= 2 && tok[1] == "calibrate") {
        smartrc::sensors::recalibrateImu();
    }
    else if (cmd == "imu" && n >= 2 && tok[1] == "watch")  cmdImuWatch(tok, n);
    else if (cmd == "imu")        cmdImu();

    else if (cmd == "dist" && n >= 2 && tok[1] == "watch") cmdDistWatch(tok, n);
    else if (cmd == "dist")       cmdDist();

    else                          Serial.printf("unknown: %s (type 'help')\n", cmd.c_str());

    printPrompt();
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

void SerialCli::cmdStatus() {
    Serial.printf("uptime=%lu ms   heap=%u\n",
                  (unsigned long)millis(), (unsigned)ESP.getFreeHeap());
    Serial.printf("net.mode=%d connected=%d ssid=%s ip=%s rssi=%d\n",
                  (int)deps_.network->mode(),
                  (int)deps_.network->isConnected(),
                  deps_.network->ssid().c_str(),
                  deps_.network->ip().toString().c_str(),
                  (int)deps_.network->rssi());
    Serial.printf("drive.moving=%d\n", (int)deps_.drive->isMoving());
    Serial.printf("steer.state=%d lastDir=%d\n",
                  (int)deps_.steering->state(),
                  (int)deps_.steering->lastDirection());
    Serial.printf("safety.emergency=%d stale=%d\n",
                  (int)deps_.safety->isEmergency(),
                  (int)deps_.safety->isStale());
}

void SerialCli::cmdConfigShow() {
    const Config& c = *deps_.config;
    Serial.printf("wifiSsid=%s\n",            c.wifiSsid.c_str());
    Serial.printf("wifiPassword=%s\n",        c.wifiPassword.length() ? "<set>" : "<empty>");
    Serial.printf("apSsid=%s\n",              c.apSsid.c_str());
    Serial.printf("apPassword=%s\n",          c.apPassword.length() ? "<set>" : "<empty>");
    Serial.printf("hostname=%s\n",            c.hostname.c_str());
    Serial.printf("useDhcp=%d\n",             (int)c.useDhcp);
    Serial.printf("staticIp=%s\n",            c.staticIp.toString().c_str());
    Serial.printf("staticGateway=%s\n",       c.staticGateway.toString().c_str());
    Serial.printf("steeringPulseMs=%u\n",     (unsigned)c.steeringPulseMs);
    Serial.printf("steeringCooldownMs=%u\n",  (unsigned)c.steeringCooldownMs);
    Serial.printf("defaultDrivePwm=%u\n",     (unsigned)c.defaultDrivePwm);
    Serial.printf("defaultSteerPwm=%u\n",     (unsigned)c.defaultSteerPwm);
    Serial.printf("heartbeatTimeoutMs=%u\n",  (unsigned)c.heartbeatTimeoutMs);
    Serial.printf("driveInverted=%d\n",       (int)c.driveInverted);
    Serial.printf("steerInverted=%d\n",       (int)c.steerInverted);
    Serial.printf("apShutdownAfterProv=%d\n", (int)c.apShutdownAfterProvision);
    Serial.printf("controlToken=%s\n",        c.controlToken.length() ? "<set>" : "<empty>");
    Serial.printf("logLevel=%u\n",            (unsigned)c.logLevel);
}

void SerialCli::cmdDrive(const char* action, const String* argv, int argc) {
    uint8_t speed = argc >= 2 ? parseSpeed(argv[1]) : 0;
    Command cmd = CommandHandler::parse(action);
    auto res = deps_.commands->execute(cmd, speed);
    Serial.printf("%s -> %s (%s)\n", action,
                  res.accepted ? "ok" : "reject",
                  res.reason ? res.reason : "");
}

void SerialCli::cmdSteer(const char* action, const String* argv, int argc) {
    uint8_t speed = argc >= 2 ? parseSpeed(argv[1]) : 0;
    Command cmd = CommandHandler::parse(action);
    auto res = deps_.commands->execute(cmd, speed);
    Serial.printf("%s -> %s (%s)\n", action,
                  res.accepted ? "ok" : "reject",
                  res.reason ? res.reason : "");
}

void SerialCli::cmdEstop(bool latch) {
    Command cmd = latch ? Command::EmergencyStop : Command::ClearEmergency;
    auto res = deps_.commands->execute(cmd);
    Serial.printf("%s -> %s\n", latch ? "estop" : "clear",
                  res.accepted ? "ok" : "reject");
}

void SerialCli::cmdNet() {
    Serial.printf("mode=%d ip=%s ssid=%s rssi=%d hostname=%s\n",
                  (int)deps_.network->mode(),
                  deps_.network->ip().toString().c_str(),
                  deps_.network->ssid().c_str(),
                  (int)deps_.network->rssi(),
                  deps_.network->hostname().c_str());
}

void SerialCli::cmdScan() {
    Serial.println("scanning...");
    int n = deps_.network->scanNetworks(
        [](const char* ssid, int32_t rssi, bool secure, uint8_t channel) {
            if (!ssid || !ssid[0]) return;
            Serial.printf("  %-32s rssi=%-4d ch=%-2u %s\n",
                          ssid, (int)rssi, (unsigned)channel,
                          secure ? "[secure]" : "[open]");
        });
    Serial.printf("(%d networks found)\n", n < 0 ? 0 : n);
}

void SerialCli::cmdConnect(const String* argv, int argc) {
    if (argc < 2) { Serial.println("usage: connect <ssid> [pass]"); return; }
    String ssid = argv[1];
    String pass = argc >= 3 ? argv[2] : String("");
    Serial.printf("connecting to %s ...\n", ssid.c_str());
    auto r = deps_.network->tryConnect(ssid, pass);
    if (r.ok) {
        deps_.config->wifiSsid     = ssid;
        deps_.config->wifiPassword = pass;
        const bool saved = saveConfig(*deps_.config);
        Serial.printf("OK ip=%s rssi=%d saved=%d\n",
                      r.ip.toString().c_str(), (int)r.rssi, (int)saved);
    } else {
        Serial.printf("FAILED: %s\n", r.reason.c_str());
    }
}

void SerialCli::cmdReboot() {
    Serial.println("rebooting...");
    Serial.flush();
    delay(100);
    ESP.restart();
}

void SerialCli::cmdFactory() {
    Serial.println("factory reset — wiping NVS and rebooting");
    factoryReset();
    Serial.flush();
    delay(100);
    ESP.restart();
}

void SerialCli::cmdNetReset() {
    networkReset();
    Serial.println("Wi-Fi/IP config wiped. 'reboot' to apply.");
}

void SerialCli::cmdLogLevel(const String* argv, int argc) {
    if (argc < 2) {
        Serial.printf("logLevel=%u (0=off..4=debug)\n",
                      (unsigned)deps_.config->logLevel);
        return;
    }
    int v = argv[1].toInt();
    if (v < 0 || v > 4) { Serial.println("range 0..4"); return; }
    deps_.config->logLevel = (uint8_t)v;
    saveConfig(*deps_.config);
    Serial.printf("logLevel=%u (saved)\n", (unsigned)v);
}

void SerialCli::cmdI2cScan() {
    Serial.println("scanning I2C bus 0x08..0x77...");
    int count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        Wire.beginTransmission(addr);
        const uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("  0x%02X  %s\n", addr,
                          addr == 0x68 ? "(MPU6050)" :
                          addr == 0x69 ? "(MPU6050 alt)" : "");
            ++count;
        }
    }
    Serial.printf("(%d device%s found)\n", count, count == 1 ? "" : "s");
}

void SerialCli::cmdImu() {
    const smartrc::sensors::Mpu6050& m = smartrc::sensors::imu();
    if (!m.present()) {
        Serial.println("IMU not present (failed init — try 'i2c scan')");
        return;
    }
    const auto& s = m.last();
    Serial.printf("accel (m/s^2)  x=%+7.3f  y=%+7.3f  z=%+7.3f\n",
                  s.ax, s.ay, s.az);
    Serial.printf("gyro  (deg/s)  x=%+7.3f  y=%+7.3f  z=%+7.3f\n",
                  s.gx, s.gy, s.gz);
    Serial.printf("temp          %.2f degC    valid=%d    age=%lu ms\n",
                  s.temperature_c, (int)s.valid,
                  (unsigned long)(millis() - s.ts_ms));
    Serial.printf("inverts        x=%d y=%d z=%d\n",
                  (int)m.invertX(), (int)m.invertY(), (int)m.invertZ());
    Serial.printf("gyro bias     x=%+6.2f y=%+6.2f z=%+6.2f deg/s  (cal=%d)\n",
                  m.gyroBiasDps(0), m.gyroBiasDps(1), m.gyroBiasDps(2),
                  (int)m.gyroCalibrated());
    Serial.printf("velocity X    %+6.3f m/s   stationary=%d\n",
                  m.velocityX(), (int)m.isStationary());
}

void SerialCli::cmdImuWatch(const String* argv, int argc) {
    int seconds = (argc >= 3) ? argv[2].toInt() : 6;
    if (seconds < 1) seconds = 1;
    if (seconds > 30) seconds = 30;
    const uint32_t now = millis();
    watchStartMs_ = now;
    watchNextMs_  = now;          // fire first line immediately
    watchEndMs_   = now + (uint32_t)seconds * 1000;
    watchKind_    = Watch::Imu;
    Serial.printf(
        "[watch] streaming IMU for %d s — drive or push the car now\n", seconds);
    Serial.println(
        "  ts    ax     ay     az     vx     stat");
    Serial.println(
        "----  ------ ------ ------ ------ ----");
}

void SerialCli::cmdDist() {
    auto& d = smartrc::sensors::distance();
    using Slot = smartrc::sensors::DistanceSensors::Slot;
    Serial.printf("front  present=%d  valid=%d  mm=%u\n",
                  (int)d.present(Slot::Front),
                  (int)d.valid(Slot::Front),
                  (unsigned)d.lastMm(Slot::Front));
    Serial.printf("rear   present=%d  valid=%d  mm=%u\n",
                  (int)d.present(Slot::Rear),
                  (int)d.valid(Slot::Rear),
                  (unsigned)d.lastMm(Slot::Rear));
}

void SerialCli::cmdDistWatch(const String* argv, int argc) {
    int seconds = (argc >= 3) ? argv[2].toInt() : 6;
    if (seconds < 1)  seconds = 1;
    if (seconds > 60) seconds = 60;
    const uint32_t now = millis();
    watchStartMs_ = now;
    watchNextMs_  = now;
    watchEndMs_   = now + (uint32_t)seconds * 1000;
    watchKind_    = Watch::Dist;
    Serial.printf(
        "[watch] streaming distance for %d s — move an obstacle in/out\n",
        seconds);
}

void SerialCli::cmdToken(const String* argv, int argc) {
    if (argc < 2) {
        Serial.printf("controlToken=%s\n",
                      deps_.config->controlToken.length() ? "<set>" : "<empty>");
        return;
    }
    if (argv[1] == "clear") deps_.config->controlToken = "";
    else                    deps_.config->controlToken = argv[1];
    saveConfig(*deps_.config);
    Serial.printf("controlToken %s (saved)\n",
                  deps_.config->controlToken.length() ? "set" : "cleared");
}

}  // namespace smartrc
