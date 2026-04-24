#pragma once

#include <stdint.h>

#include "motors/MotorDriver.h"

namespace smartrc {

// Rear motor (TB6612 channel B): forward / reverse / stop.
// Stateless wrapper around MotorDriver — no timers.
class Drive {
public:
    void begin(MotorDriver* driver, uint8_t defaultPwm);

    void forward(uint8_t speed = 0);  // speed=0 -> use default
    void reverse(uint8_t speed = 0);
    void stop();
    void brake();

    void setDefaultPwm(uint8_t pwm) { defaultPwm_ = pwm; }
    uint8_t defaultPwm() const { return defaultPwm_; }

    // Swap electrical polarity. "forward" still means forward logically —
    // only the MotorDir handed to the H-bridge flips.
    void setInverted(bool inverted) { inverted_ = inverted; }
    bool inverted() const { return inverted_; }

    bool isMoving() const { return moving_; }

private:
    MotorDriver* driver_   = nullptr;
    uint8_t      defaultPwm_ = 0;
    bool         moving_     = false;
    bool         inverted_   = false;
};

}  // namespace smartrc
