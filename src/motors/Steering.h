#pragma once

#include <stdint.h>

#include "motors/MotorDriver.h"

namespace smartrc {

// Front motor (TB6612 channel A) used as a position-less steering actuator.
//
// Steering is OPEN-LOOP HOLD — pulseLeft/pulseRight energise the motor and
// keep it on until either (a) Steering::stop() is called (e.g. from the
// `steer_stop` command on button release) or (b) Safety's heartbeat
// watchdog stops everything for command staleness. Calling the opposite
// direction while energised interrupts and reverses instantly.
//
// `pulseMs` is retained as a last-resort emergency cut-out at 5× the set
// value — it should never trigger in normal use because Safety's
// heartbeat watchdog (default 800 ms) always wins first.
// `cooldownMs` is a post-stop coast window (informational) that does not
// gate user input.
//
// Non-blocking: callers must invoke update() regularly from loop().

class Steering {
public:
    enum class Dir   : uint8_t { None, Left, Right };
    enum class State : uint8_t { Idle, Pulsing, Cooldown };

    void begin(MotorDriver* driver,
               uint16_t pulseMs,
               uint16_t cooldownMs,
               uint8_t  defaultPwm);

    // Request a pulse. Returns false if rejected (reverse-direction
    // cooldown not yet elapsed, or another pulse currently active).
    bool pulseLeft(uint8_t speed = 0);
    bool pulseRight(uint8_t speed = 0);

    // Force-stop the motor and cancel any in-flight pulse.
    void stop();

    // Tick the state machine. Cheap; safe to call every loop().
    void update();

    // Live tuning.
    void setPulseDuration(uint16_t ms)   { pulseMs_    = ms; }
    void setCooldown(uint16_t ms)        { cooldownMs_ = ms; }
    void setDefaultPwm(uint8_t pwm)      { defaultPwm_ = pwm; }

    // Swap left/right electrically without rewiring. Logical API unchanged.
    void setInverted(bool inverted)      { inverted_ = inverted; }
    bool inverted() const { return inverted_; }

    State state() const { return state_; }
    Dir   lastDirection() const { return lastDir_; }

private:
    bool startPulse(Dir d, uint8_t speed);

    MotorDriver* driver_     = nullptr;
    State        state_      = State::Idle;
    Dir          dir_        = Dir::None;
    Dir          lastDir_    = Dir::None;
    uint32_t     stateStartMs_ = 0;
    uint16_t     pulseMs_    = 600;
    uint16_t     cooldownMs_ = 120;
    uint8_t      defaultPwm_ = 220;
    bool         inverted_   = false;
};

}  // namespace smartrc
