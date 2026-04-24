#pragma once

#include <stdint.h>

namespace smartrc {

class Drive;
class Steering;

// Watchdog over command freshness + global e-stop.
//
// Every accepted control command must call notifyHeartbeat(). If the gap
// between heartbeats exceeds `timeoutMs`, both motors are forced to stop.
// emergencyStop() latches motors off until clearEmergency() is called.
class Safety {
public:
    void begin(Drive* drive, Steering* steering, uint16_t timeoutMs);

    void notifyHeartbeat();        // call on every accepted command
    void emergencyStop();          // hard stop + latch
    void clearEmergency();
    void setTimeout(uint16_t ms) { timeoutMs_ = ms; }

    // Returns true while no commands have been received within timeoutMs.
    bool isStale() const;
    bool isEmergency() const { return emergency_; }

    // Tick from loop(). Issues stops as needed; cheap if nothing changed.
    void update();

private:
    Drive*    drive_      = nullptr;
    Steering* steering_   = nullptr;
    uint32_t  lastBeatMs_ = 0;
    uint16_t  timeoutMs_  = 1500;
    bool      emergency_  = false;
    bool      stoppedForStale_ = false;
};

}  // namespace smartrc
