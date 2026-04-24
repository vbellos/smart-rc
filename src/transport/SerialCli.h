#pragma once

#include <Arduino.h>

namespace smartrc {

struct Config;
class CommandHandler;
class Drive;
class Steering;
class Safety;
class NetworkManager;

struct SerialCliDeps {
    Config*         config;
    CommandHandler* commands;
    Drive*          drive;
    Steering*       steering;
    Safety*         safety;
    NetworkManager* network;
};

// Interactive serial console. Lets you drive the RC, inspect config,
// scan / connect Wi-Fi, and read telemetry over USB without a browser.
// Useful for:
//   - debugging when Wi-Fi is broken (no portal access)
//   - bench testing motors
//   - poking NVS config quickly
//   - watching live events (estop, stale, net changes)
//
// Usage from a serial monitor @ 115200:
//   > help
//   > status
//   > fwd 200
//   > left
//   > stop
//   > scan
//   > connect MyWifi hunter2
class SerialCli {
public:
    void begin(const SerialCliDeps& deps);

    // Pump from loop(). Non-blocking: reads whatever's in the Serial
    // buffer, assembles a line, dispatches on CR/LF.
    void update();

private:
    void printPrompt();
    void printHelp();
    void processLine(const String& line);

    // Command handlers.
    void cmdStatus();
    void cmdConfigShow();
    void cmdDrive(const char* action, const String* argv, int argc);
    void cmdSteer(const char* action, const String* argv, int argc);
    void cmdEstop(bool latch);
    void cmdNet();
    void cmdScan();
    void cmdConnect(const String* argv, int argc);
    void cmdReboot();
    void cmdFactory();
    void cmdNetReset();
    void cmdLogLevel(const String* argv, int argc);
    void cmdToken(const String* argv, int argc);
    void cmdI2cScan();
    void cmdImu();

    SerialCliDeps deps_{};
    String        buf_;
    bool          promptShown_ = false;
    static constexpr size_t kMaxLine = 256;
};

}  // namespace smartrc
