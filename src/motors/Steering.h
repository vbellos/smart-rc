#pragma once

#include <stdint.h>

#include "motors/MotorDriver.h"

namespace smartrc {

// Front motor (TB6612 channel A) used as a position-less steering actuator.
//
// Steering is OPEN-LOOP TIMED PULSES — not angle / not servo control.
// A "left" or "right" command energises the motor for `pulseMs` and then
// stops it. pulseLeft/pulseRight are always accepted — calling the opposite
// direction while a pulse is running interrupts it and reverses instantly.
// `cooldownMs` is now an informational "post-pulse coast window" in the
// state machine (useful for status reporting); it does NOT gate user input.
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
    uint16_t     pulseMs_    = 400;
    uint16_t     cooldownMs_ = 120;
    uint8_t      defaultPwm_ = 220;
    bool         inverted_   = false;
};

}  // namespace smartrc
