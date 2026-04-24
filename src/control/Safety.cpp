#include "control/Safety.h"

#include <Arduino.h>

#include "motors/Drive.h"
#include "motors/Steering.h"

namespace smartrc {

void Safety::begin(Drive* drive, Steering* steering, uint16_t timeoutMs) {
    drive_      = drive;
    steering_   = steering;
    timeoutMs_  = timeoutMs;
    lastBeatMs_ = millis();
    emergency_  = false;
    stoppedForStale_ = false;
}

void Safety::notifyHeartbeat() {
    lastBeatMs_      = millis();
    stoppedForStale_ = false;
}

void Safety::emergencyStop() {
    emergency_ = true;
    if (drive_)    drive_->brake();
    if (steering_) steering_->stop();
}

void Safety::clearEmergency() {
    emergency_       = false;
    lastBeatMs_      = millis();
    stoppedForStale_ = false;
}

bool Safety::isStale() const {
    return (millis() - lastBeatMs_) > timeoutMs_;
}

void Safety::update() {
    if (emergency_) {
        if (drive_    && drive_->isMoving())                    drive_->brake();
        if (steering_ && steering_->state() == Steering::State::Pulsing) steering_->stop();
        return;
    }
    if (isStale() && !stoppedForStale_) {
        if (drive_)    drive_->stop();
        if (steering_) steering_->stop();
        stoppedForStale_ = true;
    }
}

}  // namespace smartrc
