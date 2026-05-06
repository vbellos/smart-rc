#pragma once

#include <stdint.h>

namespace smartrc {

class Drive;
class Steering;
class Safety;
class AutoBrake;

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
    void begin(Drive* drive, Steering* steering, Safety* safety,
               AutoBrake* autoBrake = nullptr);

    CommandResult execute(Command cmd, uint8_t speed = 0);

    // Map a textual action ("forward", "left", ...) to a Command.
    static Command parse(const char* action);

    // Reset both directional override counters. Used by tests + the
    // emergency-stop path so a clear_estop doesn't carry stale tap
    // counts forward.
    void resetOverrideTaps();

private:
    // 3-tap override state, per-direction. A "tap" is a Forward command
    // (or Reverse for the rear) preceded by a different command — held
    // throttle that auto-repeats Forward → Forward → Forward registers
    // as a single tap, not three.
    struct TapState {
        uint8_t  count       = 0;
        uint32_t lastTapMs   = 0;
    };
    void registerOverrideTap(TapState& s, bool isFront);

    Drive*     drive_      = nullptr;
    Steering*  steering_   = nullptr;
    Safety*    safety_     = nullptr;
    AutoBrake* autoBrake_  = nullptr;

    // Last command we received (accepted OR rejected) — used to gate
    // tap detection so held-throttle repeats don't accidentally arm
    // the override.
    Command   lastCmd_     = Command::Unknown;
    TapState  frontTaps_   = {};
    TapState  rearTaps_    = {};
};

}  // namespace smartrc
