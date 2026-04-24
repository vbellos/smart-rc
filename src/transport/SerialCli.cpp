#include "transport/SerialCli.h"

#include "config/Config.h"
#include "control/CommandHandler.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"
#include "network/NetworkManager.h"

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
        "  token [<new> | clear]      show / set / clear controlToken\n");
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
