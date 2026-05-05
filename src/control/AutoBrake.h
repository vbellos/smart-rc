#pragma once

#include <stdint.h>

namespace smartrc {

class Drive;
namespace sensors { class DistanceSensors; class Mpu6050; }

// Forward-collision avoidance using the front VL53L0X.
//
// When enabled, on every loop tick we compute a speed-dependent trigger
// distance:
//
//     trigger_cm = baseCm + slopeCmPerMs * max(0, vx_ms)
//
// If the front reading is at-or-below that trigger AND the vehicle is
// moving forward faster than minSpeedCmPs, we call Drive::brake() (which
// uses the IMU-aware active-brake state machine to bring the car to rest)
// and expose `engaged() == true` so CommandHandler can reject further
// `forward` requests until either the vehicle stops or the obstacle moves
// away. Reverse + steering remain available.
//
// AutoBrake does NOT latch like Safety::emergencyStop(). It re-evaluates
// every tick — disengage is automatic.
class AutoBrake {
public:
    void begin(Drive* drive,
               sensors::DistanceSensors* dist,
               sensors::Mpu6050* imu);

    /** Pump from loop(). Cheap when disabled. */
    void update();

    void setEnabled(bool e) { enabled_ = e; if (!e) engaged_ = false; }
    bool enabled() const    { return enabled_; }

    /** Live tunables — applied without reboot from the portal/CLI. */
    void setParams(uint16_t baseCm,
                   uint16_t slopeCmPerMs,
                   uint16_t minSpeedCmPs) {
        baseCm_       = baseCm;
        slopeCmPerMs_ = slopeCmPerMs;
        minSpeedCmPs_ = minSpeedCmPs;
    }

    /** True while AutoBrake is actively forcing the brake. */
    bool engaged() const     { return engaged_; }

    /** Most-recent computed trigger distance (cm). Useful for UI. */
    uint16_t triggerCm() const { return triggerCm_; }

    /** Most-recent front distance read (cm). 0xFFFF if unknown. */
    uint16_t distanceCm() const { return distanceCm_; }

private:
    Drive*                     drive_ = nullptr;
    sensors::DistanceSensors*  dist_  = nullptr;
    sensors::Mpu6050*          imu_   = nullptr;

    bool     enabled_     = false;
    bool     engaged_     = false;
    uint16_t baseCm_      = 20;
    uint16_t slopeCmPerMs_= 30;
    uint16_t minSpeedCmPs_= 10;

    // Last-evaluated state, surfaced via getters for telemetry/UI.
    uint16_t triggerCm_   = 0;
    uint16_t distanceCm_  = 0xFFFF;

    // Rate-limit re-issuing brake() while engaged so we don't spam the
    // active-brake state machine on every tick.
    uint32_t lastBrakeMs_ = 0;
};

}  // namespace smartrc
