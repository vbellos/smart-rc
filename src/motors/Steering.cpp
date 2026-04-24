#include "motors/Steering.h"

#include <Arduino.h>

namespace smartrc {

void Steering::begin(MotorDriver* driver,
                     uint16_t pulseMs,
                     uint16_t cooldownMs,
                     uint8_t  defaultPwm) {
    driver_       = driver;
    pulseMs_      = pulseMs;
    cooldownMs_   = cooldownMs;
    defaultPwm_   = defaultPwm;
    state_        = State::Idle;
    dir_          = Dir::None;
    lastDir_      = Dir::None;
    stateStartMs_ = 0;
    stop();
}

bool Steering::pulseLeft(uint8_t speed) {
    return startPulse(Dir::Left, speed);
}

bool Steering::pulseRight(uint8_t speed) {
    return startPulse(Dir::Right, speed);
}

bool Steering::startPulse(Dir d, uint8_t speed) {
    if (!driver_ || d == Dir::None) return false;
    if (speed == 0) speed = defaultPwm_;

    const uint32_t now = millis();

    // Same direction while already pulsing: extend the pulse window so a
    // held key keeps the wheel deflected rather than pulse/cooldown flicker.
    if (state_ == State::Pulsing && dir_ == d) {
        stateStartMs_ = now;
        return true;
    }

    // Opposite direction during an active pulse, OR during cooldown, OR
    // from idle — always honour immediately. User-initiated reversals are
    // never rejected; the H-bridge handles the instant direction flip.
    const bool logicalLeft = (d == Dir::Left);
    const bool physicalLeft = inverted_ ? !logicalLeft : logicalLeft;
    const MotorDir physical = physicalLeft ? MotorDir::Reverse : MotorDir::Forward;

    driver_->set(MotorChannel::A, physical, speed);

    state_        = State::Pulsing;
    dir_          = d;
    stateStartMs_ = now;
    return true;
}

void Steering::stop() {
    if (driver_) driver_->set(MotorChannel::A, MotorDir::Coast, 0);
    if (state_ == State::Pulsing) {
        lastDir_      = dir_;
        state_        = State::Cooldown;
        stateStartMs_ = millis();
    } else {
        state_ = State::Idle;
    }
    dir_ = Dir::None;
}

void Steering::update() {
    if (state_ == State::Idle) return;

    const uint32_t now     = millis();
    const uint32_t elapsed = now - stateStartMs_;

    if (state_ == State::Pulsing && elapsed >= pulseMs_) {
        // End pulse -> coast. Enter cooldown only as an informational
        // state; it no longer gates opposite-direction requests.
        if (driver_) driver_->set(MotorChannel::A, MotorDir::Coast, 0);
        lastDir_      = dir_;
        dir_          = Dir::None;
        state_        = State::Cooldown;
        stateStartMs_ = now;
        return;
    }

    if (state_ == State::Cooldown && elapsed >= cooldownMs_) {
        state_ = State::Idle;
    }
}

}  // namespace smartrc
