#pragma once

#include <stdint.h>

namespace smartrc {

class Drive;
namespace sensors { class DistanceSensors; class Mpu6050; }

// Forward + rear collision avoidance using the front/rear VL53L0X.
//
// On every loop tick each side independently computes a speed-dependent
// trigger distance:
//
//     front trigger_cm = frontBase + frontSlope * max(0, +vx_ms)
//     rear  trigger_cm = rearBase  + rearSlope  * max(0, -vx_ms)
//
// If a side's distance reading is at-or-below its trigger AND the vehicle
// is travelling fast enough toward that side, AutoBrake calls
// Drive::brake() and exposes activeFront() / activeRear() so the
// CommandHandler can reject the matching `forward` or `reverse` request
// until the vehicle settles or the obstacle moves.
//
// The opposite-direction command always remains available so the driver
// can back away from the obstacle they're stuck on. Does NOT latch.
class AutoBrake {
public:
    enum Side : uint8_t { Front = 0, Rear = 1, kCount = 2 };

    void begin(Drive* drive,
               sensors::DistanceSensors* dist,
               sensors::Mpu6050* imu);

    /** Pump from loop(). Cheap when disabled. */
    void update();

    void setEnabled(bool e);
    bool enabled() const { return enabled_; }

    /** Live tunables, applied without reboot from the portal/CLI. */
    void setFrontParams(uint16_t baseCm, uint16_t slopeCmPerMs,
                        uint16_t minSpeedCmPs) {
        front_.baseCm       = baseCm;
        front_.slopeCmPerMs = slopeCmPerMs;
        front_.minSpeedCmPs = minSpeedCmPs;
    }
    void setRearParams (uint16_t baseCm, uint16_t slopeCmPerMs,
                        uint16_t minSpeedCmPs) {
        rear_.baseCm       = baseCm;
        rear_.slopeCmPerMs = slopeCmPerMs;
        rear_.minSpeedCmPs = minSpeedCmPs;
    }

    /** True while EITHER side is forcing the brake. */
    bool engaged() const { return front_.engaged || rear_.engaged; }

    bool activeFront() const { return front_.engaged; }
    bool activeRear()  const { return rear_.engaged;  }

    /** Most-recent computed trigger distance for the named side (cm). */
    uint16_t triggerCm(Side s) const {
        return (s == Front) ? front_.triggerCm : rear_.triggerCm;
    }

    /** Most-recent distance read for the named side (cm); 0xFFFF unknown. */
    uint16_t distanceCm(Side s) const {
        return (s == Front) ? front_.distanceCm : rear_.distanceCm;
    }

private:
    struct SideState {
        // tunables
        uint16_t baseCm        = 20;
        uint16_t slopeCmPerMs  = 30;
        uint16_t minSpeedCmPs  = 10;
        // last evaluation
        bool     engaged       = false;
        uint16_t triggerCm     = 0;
        uint16_t distanceCm    = 0xFFFF;
    };

    // Evaluate one side. `speedTowardObstacle` is the positive component
    // of vx in the side's "looking" direction (for the rear it's -vx).
    // Computes trigger/release thresholds, applies hysteresis, fires the
    // brake, and updates SideState. Reads distanceCm even when invalid
    // so the surfaced state is consistent (set to 0xFFFF beforehand).
    void evaluate(Side which, SideState& s,
                  float speedTowardObstacle,
                  bool distanceValid, uint16_t distanceCm);

    Drive*                     drive_ = nullptr;
    sensors::DistanceSensors*  dist_  = nullptr;
    sensors::Mpu6050*          imu_   = nullptr;

    bool       enabled_     = false;
    SideState  front_       = {};
    SideState  rear_        = {};

    // Rate-limit re-issuing brake() so a transport that spams the
    // forbidden direction can't tickle the active-brake state machine
    // into starvation.
    uint32_t lastBrakeMs_ = 0;
};

}  // namespace smartrc
