#include "control/AutoBrake.h"

#include <Arduino.h>

#include "motors/Drive.h"
#include "sensors/DistanceSensors.h"
#include "sensors/Mpu6050.h"

namespace smartrc {

namespace {
constexpr uint32_t kBrakeRepeatMs = 200;

const char* sideLabel(AutoBrake::Side s) {
    return (s == AutoBrake::Front) ? "front" : "rear";
}
}  // namespace

void AutoBrake::begin(Drive* drive,
                      sensors::DistanceSensors* dist,
                      sensors::Mpu6050* imu) {
    drive_ = drive;
    dist_  = dist;
    imu_   = imu;
}

void AutoBrake::setEnabled(bool e) {
    enabled_ = e;
    if (!e) {
        front_.engaged = false;
        rear_.engaged  = false;
    }
}

void AutoBrake::update() {
    if (!enabled_ || !drive_ || !dist_ || !imu_) {
        front_.engaged = false;
        rear_.engaged  = false;
        return;
    }

    using Slot = sensors::DistanceSensors::Slot;

    // Read both sensors. Treat invalid/absent as "unknown — don't trigger"
    // — refusing to brake on a stale reading is the safer default; the
    // alternative is phantom-braking when the sensor flickers.
    const bool f_ok = dist_->valid(Slot::Front);
    const bool r_ok = dist_->valid(Slot::Rear);
    const uint16_t f_cm = f_ok ? (uint16_t)(dist_->lastMm(Slot::Front) / 10) : 0xFFFF;
    const uint16_t r_cm = r_ok ? (uint16_t)(dist_->lastMm(Slot::Rear)  / 10) : 0xFFFF;

    // Forward velocity sign convention: +vx = forward, -vx = reverse.
    const float vx = imu_->velocityX();

    // Front side: only evaluate when moving forward.
    {
        const float vFwd = vx > 0.0f ? vx : 0.0f;
        front_.triggerCm  = front_.baseCm +
                            (uint16_t)((front_.slopeCmPerMs * vFwd) + 0.5f);
        front_.distanceCm = f_cm;
        const uint16_t v_cmps = (uint16_t)((vFwd * 100.0f) + 0.5f);
        const bool moving    = v_cmps >= front_.minSpeedCmPs;
        const bool close     = f_ok && f_cm <= front_.triggerCm;
        evaluate(Front, front_, vFwd, moving && close, f_cm);
    }

    // Rear side: only evaluate when moving backward.
    {
        const float vRev = vx < 0.0f ? -vx : 0.0f;
        rear_.triggerCm  = rear_.baseCm +
                           (uint16_t)((rear_.slopeCmPerMs * vRev) + 0.5f);
        rear_.distanceCm = r_cm;
        const uint16_t v_cmps = (uint16_t)((vRev * 100.0f) + 0.5f);
        const bool moving    = v_cmps >= rear_.minSpeedCmPs;
        const bool close     = r_ok && r_cm <= rear_.triggerCm;
        evaluate(Rear, rear_, vRev, moving && close, r_cm);
    }
}

void AutoBrake::evaluate(Side which, SideState& s,
                         float speedTowardObstacle, bool shouldEngage,
                         uint16_t distanceCm) {
    if (shouldEngage) {
        const uint32_t now = millis();
        if (!s.engaged) {
            s.engaged    = true;
            lastBrakeMs_ = now;
            drive_->brake();
            Serial.printf(
                "[autobrake.%s] ENGAGED  d=%u cm  trigger=%u cm  v=%.2f m/s\n",
                sideLabel(which), distanceCm, s.triggerCm, speedTowardObstacle);
        } else if (drive_->isMoving() && (now - lastBrakeMs_ >= kBrakeRepeatMs)) {
            drive_->brake();
            lastBrakeMs_ = now;
        }
    } else if (s.engaged) {
        s.engaged = false;
        Serial.printf(
            "[autobrake.%s] released  d=%u cm  v=%.2f m/s\n",
            sideLabel(which), distanceCm, speedTowardObstacle);
    }
}

}  // namespace smartrc
