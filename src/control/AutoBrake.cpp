#include "control/AutoBrake.h"

#include <Arduino.h>

#include "motors/Drive.h"
#include "sensors/DistanceSensors.h"
#include "sensors/Mpu6050.h"

namespace smartrc {

namespace {
constexpr uint32_t kBrakeRepeatMs = 200;

// Hysteresis multiplier: once engaged, stay engaged until distance
// exceeds baseCm * this. Prevents the "creep + brake" loop where the
// user holds throttle, brake fires, car stops just past the trigger
// distance, brake disengages, motor drives forward again, etc.
// 1.5 means a base of 20 cm releases at 30 cm — 10 cm of margin, scaled
// to whatever the user has tuned the base to.
constexpr float kReleaseMultiplier = 1.5f;

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

void AutoBrake::bypass(Side which, uint32_t durationMs) {
    SideState& s = (which == Front) ? front_ : rear_;
    s.bypassEndMs = millis() + durationMs;
    s.engaged     = false;  // drop the gate immediately
    Serial.printf("[autobrake.%s] bypass armed for %u ms\n",
                  sideLabel(which), (unsigned)durationMs);
}

bool AutoBrake::bypassed(Side which) const {
    const SideState& s = (which == Front) ? front_ : rear_;
    return s.bypassEndMs != 0 && (int32_t)(millis() - s.bypassEndMs) < 0;
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

    // Front side: positive vx is forward (toward the front sensor).
    evaluate(Front, front_, vx > 0.0f ? vx : 0.0f, f_ok, f_cm);
    // Rear side: negative vx is reverse (toward the rear sensor).
    evaluate(Rear,  rear_,  vx < 0.0f ? -vx : 0.0f, r_ok, r_cm);
}

void AutoBrake::evaluate(Side which, SideState& s,
                         float speedTowardObstacle, bool distanceValid,
                         uint16_t distanceCm) {
    // Compute trigger and release thresholds.
    //   trigger = base + slope * |speed|   ← scales with how fast we're approaching
    //   release = base * 1.5               ← fixed hysteresis ceiling
    s.triggerCm  = s.baseCm +
                   (uint16_t)((s.slopeCmPerMs * speedTowardObstacle) + 0.5f);
    s.distanceCm = distanceCm;
    const uint16_t releaseCm = (uint16_t)(s.baseCm * kReleaseMultiplier);

    // 3-tap override active for this side — short-circuit. Driver has
    // explicitly opted in to push through; we surface the most-recent
    // distance/trigger for telemetry but don't gate the motor.
    if (s.bypassEndMs != 0) {
        if ((int32_t)(millis() - s.bypassEndMs) < 0) {
            s.engaged = false;
            return;
        }
        // Window expired — clear the marker and re-evaluate normally.
        s.bypassEndMs = 0;
    }

    // Engagement decision differs based on current state — it's the
    // hysteresis band that prevents bouncing between engaged/released
    // as the car decelerates past the trigger.
    bool shouldEngage = false;
    if (distanceValid) {
        if (s.engaged) {
            // Already engaged — hold until obstacle is clearly past the
            // release threshold. This is what keeps the car locked when
            // the user is holding throttle into a stationary obstacle:
            // brake fires, car stops, distance is still inside release,
            // we stay engaged, CommandHandler keeps rejecting forward.
            shouldEngage = distanceCm <= releaseCm;
        } else {
            // Not engaged yet — two paths to engagement:
            //   a) actively closing on the obstacle fast enough to need
            //      stopping room (distance < trigger AND moving), or
            //   b) parked too close already (distance < base) — arm
            //      immediately so the next throttle press is rejected
            //      and the car can't even start moving toward it.
            const uint16_t v_cmps =
                (uint16_t)((speedTowardObstacle * 100.0f) + 0.5f);
            const bool moving       = v_cmps >= s.minSpeedCmPs;
            const bool approachClose = distanceCm <= s.triggerCm;
            const bool tooCloseAtRest = distanceCm <= s.baseCm;
            shouldEngage = (moving && approachClose) || tooCloseAtRest;
        }
    }

    if (shouldEngage) {
        const uint32_t now = millis();
        if (!s.engaged) {
            s.engaged    = true;
            lastBrakeMs_ = now;
            // Only call brake() if the car is actually moving. A parked
            // car with an obstacle in front shouldn't fire a spurious
            // active-brake event — Drive::brake() would no-op anyway,
            // but we also want a clean log.
            if (drive_->isMoving()) drive_->brake();
            Serial.printf(
                "[autobrake.%s] ENGAGED  d=%u cm  trigger=%u cm  v=%.2f m/s\n",
                sideLabel(which), distanceCm, s.triggerCm,
                speedTowardObstacle);
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
