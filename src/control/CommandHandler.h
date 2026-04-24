#pragma once

#include <stdint.h>

namespace smartrc {

class Drive;
class Steering;
class Safety;

enum class Command : uint8_t {
    Unknown,
    Forward,
    Reverse,
    Stop,
    Brake,
    Left,
    Right,
    SteerStop,       // release steering motor (for hold-and-release UIs)
    EmergencyStop,
    ClearEmergency,
};

struct CommandResult {
    bool        accepted = false;
    const char* reason   = "";
};

// Single funnel that takes a (cmd, speed) and routes it to the right
// subsystem. All accepted commands feed the safety heartbeat.
class CommandHandler {
public:
    void begin(Drive* drive, Steering* steering, Safety* safety);

    CommandResult execute(Command cmd, uint8_t speed = 0);

    // Map a textual action ("forward", "left", ...) to a Command.
    static Command parse(const char* action);

private:
    Drive*    drive_    = nullptr;
    Steering* steering_ = nullptr;
    Safety*   safety_   = nullptr;
};

}  // namespace smartrc
