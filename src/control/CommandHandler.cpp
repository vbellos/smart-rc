#include "control/CommandHandler.h"

#include <string.h>

#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"

namespace smartrc {

void CommandHandler::begin(Drive* drive, Steering* steering, Safety* safety) {
    drive_    = drive;
    steering_ = steering;
    safety_   = safety;
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

    switch (cmd) {
        case Command::Forward:
            drive_->forward(speed);
            safety_->notifyHeartbeat();
            return {true, "forward"};
        case Command::Reverse:
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

}  // namespace smartrc
