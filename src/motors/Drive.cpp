#include "motors/Drive.h"

namespace smartrc {

void Drive::begin(MotorDriver* driver, uint8_t defaultPwm) {
    driver_     = driver;
    defaultPwm_ = defaultPwm;
    stop();
}

void Drive::forward(uint8_t speed) {
    if (!driver_) return;
    if (speed == 0) speed = defaultPwm_;
    driver_->set(MotorChannel::B,
                 inverted_ ? MotorDir::Reverse : MotorDir::Forward,
                 speed);
    moving_ = true;
}

void Drive::reverse(uint8_t speed) {
    if (!driver_) return;
    if (speed == 0) speed = defaultPwm_;
    driver_->set(MotorChannel::B,
                 inverted_ ? MotorDir::Forward : MotorDir::Reverse,
                 speed);
    moving_ = true;
}

void Drive::stop() {
    if (!driver_) return;
    driver_->set(MotorChannel::B, MotorDir::Coast, 0);
    moving_ = false;
}

void Drive::brake() {
    if (!driver_) return;
    driver_->set(MotorChannel::B, MotorDir::Brake, 0);
    moving_ = false;
}

}  // namespace smartrc
