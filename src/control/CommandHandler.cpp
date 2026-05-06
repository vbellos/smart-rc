#include "control/CommandHandler.h"

#include <Arduino.h>
#include <string.h>

#include "control/AutoBrake.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"

namespace smartrc {

namespace {
// 3-tap override tuning. A burst of three discrete forward (or reverse)
// taps that all hit the auto-brake gate within kTapResetMs of each other
// arms a kBypassMs window during which AutoBrake is short-circuited and
// drive commands flow through. The driver is opting in to push past the
// sensor's verdict.
constexpr uint8_t  kTapsToOverride = 3;
constexpr uint32_t kTapResetMs     = 1500;  // tap counter resets after this idle gap
constexpr uint32_t kBypassMs       = 3000;  // bypass window after the third tap
}  // namespace

void CommandHandler::begin(Drive* drive, Steering* steering, Safety* safety,
                           AutoBrake* autoBrake) {
    drive_     = drive;
    steering_  = steering;
    safety_    = safety;
    autoBrake_ = autoBrake;
}

Command CommandHandler::parse(const char* action) {
    if (!action) return Command::Unknown;
    if (!strcmp(action, "forward"))         return Command::Forward;
    if (!strcmp(action, "reverse"))         return Command::Reverse;
    if (!strcmp(action, "stop"))            return Command::Stop;
    if (!strcmp(action, "brake"))           return Command::Brake;
    if (!strcmp(action, "left"))            return Command::Left;
    if (!strcmp(action, "right"))           return Command::Right;
    if (!strcmp(action, "steer_stop"))      return Command::SteerStop;
    if (!strcmp(action, "estop"))           return Command::EmergencyStop;
    if (!strcmp(action, "clear_estop"))     return Command::ClearEmergency;
    return Command::Unknown;
}

CommandResult CommandHandler::execute(Command cmd, uint8_t speed) {
    if (!drive_ || !steering_ || !safety_) return {false, "not initialised"};

    // ClearEmergency is the only command we accept while latched.
    if (safety_->isEmergency() && cmd != Command::ClearEmergency) {
        return {false, "emergency latched"};
    }

    // While an active brake is decelerating the car, reject new drive
    // commands so the driver can't override (and effectively cancel) the
    // brake mid-stop. Brake is short — typically <500 ms — and the brake
    // state machine has its own hard timeout (activeBrakeMaxMs), so the
    // window the user is locked out of is bounded. Steering, e-stop and
    // clear-emergency remain available throughout. Heartbeat is still
    // notified — the user is connected and tapping, just blocked.
    if (drive_->isActiveBraking()) {
        switch (cmd) {
            case Command::Forward:
            case Command::Reverse:
            case Command::Stop:
            case Command::Brake:
                safety_->notifyHeartbeat();
                return {false, "active brake in progress"};
            default:
                break;
        }
    }

    // Capture the last-command snapshot BEFORE we mutate it. Tap
    // detection compares to whatever came in last (accepted or not).
    const Command prevCmd = lastCmd_;
    lastCmd_ = cmd;

    switch (cmd) {
        case Command::Forward:
            // Reject drive into a close obstacle on the matching side.
            // Heartbeat still counts — the user is connected and trying,
            // just blocked. Letting it through keeps Safety from going
            // stale and forcing an estop mid-recovery while the driver
            // backs out the other way.
            if (autoBrake_ && autoBrake_->activeFront()) {
                // 3-tap override: a fresh tap = Forward following any
                // non-Forward command. Held-throttle repeats (Forward
                // after Forward) don't count.
                if (prevCmd != Command::Forward) {
                    registerOverrideTap(frontTaps_, /*isFront=*/true);
                }
                safety_->notifyHeartbeat();
                return {false, "auto-brake engaged (front)"};
            }
            drive_->forward(speed);
            safety_->notifyHeartbeat();
            return {true, "forward"};
        case Command::Reverse:
            if (autoBrake_ && autoBrake_->activeRear()) {
                if (prevCmd != Command::Reverse) {
                    registerOverrideTap(rearTaps_, /*isFront=*/false);
                }
                safety_->notifyHeartbeat();
                return {false, "auto-brake engaged (rear)"};
            }
            drive_->reverse(speed);
            safety_->notifyHeartbeat();
            return {true, "reverse"};
        case Command::Stop:
            drive_->stop();
            safety_->notifyHeartbeat();
            return {true, "stop"};
        case Command::Brake:
            drive_->brake();
            safety_->notifyHeartbeat();
            return {true, "brake"};
        case Command::Left:
            if (!steering_->pulseLeft(speed)) return {false, "steering busy"};
            safety_->notifyHeartbeat();
            return {true, "left"};
        case Command::Right:
            if (!steering_->pulseRight(speed)) return {false, "steering busy"};
            safety_->notifyHeartbeat();
            return {true, "right"};
        case Command::SteerStop:
            steering_->stop();
            safety_->notifyHeartbeat();
            return {true, "steer stop"};
        case Command::EmergencyStop:
            safety_->emergencyStop();
            return {true, "estop"};
        case Command::ClearEmergency:
            safety_->clearEmergency();
            return {true, "estop cleared"};
        default:
            return {false, "unknown action"};
    }
}

void CommandHandler::registerOverrideTap(TapState& s, bool isFront) {
    if (!autoBrake_) return;
    const uint32_t now = millis();
    // Idle reset — if the last tap was too long ago, this is the first
    // tap of a fresh burst.
    if (now - s.lastTapMs > kTapResetMs) s.count = 0;
    s.count++;
    s.lastTapMs = now;
    Serial.printf("[autobrake] %s override tap %u/%u\n",
                  isFront ? "front" : "rear", s.count, kTapsToOverride);
    if (s.count >= kTapsToOverride) {
        autoBrake_->bypass(isFront ? AutoBrake::Front : AutoBrake::Rear,
                           kBypassMs);
        s.count = 0;
    }
}

void CommandHandler::resetOverrideTaps() {
    frontTaps_ = {};
    rearTaps_  = {};
}

}  // namespace smartrc
