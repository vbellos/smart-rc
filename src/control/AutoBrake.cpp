#include "control/AutoBrake.h"

#include <Arduino.h>

#include "motors/Drive.h"
#include "sensors/DistanceSensors.h"
#include "sensors/Mpu6050.h"

namespace smartrc {

namespace {
// Re-issue brake() at most this often while engaged. Active-brake's own
// state machine handles the actual dynamics; this just ensures that if a
// transport keeps spamming `forward` faster than we can reject it, the
// brake intent stays asserted.
constexpr uint32_t kBrakeRepeatMs = 200;
}  // namespace

void AutoBrake::begin(Drive* drive,
                      sensors::DistanceSensors* dist,
                      sensors::Mpu6050* imu) {
    drive_ = drive;
    dist_  = dist;
    imu_   = imu;
}

void AutoBrake::update() {
    if (!enabled_ || !drive_ || !dist_ || !imu_) {
        engaged_ = false;
        return;
    }

    // Read front distance. Treat invalid/absent as "unknown — don't trigger"
    // — refusing to brake on a stale reading is the safer default; the
    // alternative would be to phantom-brake the car when the sensor flickers.
    const bool front_ok = dist_->valid(sensors::DistanceSensors::Front);
    const uint16_t front_cm = front_ok
        ? (uint16_t)(dist_->lastMm(sensors::DistanceSensors::Front) / 10)
        : 0xFFFF;

    // Forward velocity in m/s, clamped to the positive half (we never want
    // to engage while reversing — the front sensor sees nothing relevant).
    const float vx_ms  = imu_->velocityX();
    const float vx_pos = vx_ms > 0.0f ? vx_ms : 0.0f;

    // trigger_cm = base + slope * vx
    // 16-bit math is plenty: vx_pos <= ~3 m/s, slope <= ~200 cm/(m/s) →
    // worst case ~600 cm, well inside uint16_t.
    const uint16_t trigger_cm =
        baseCm_ + (uint16_t)((slopeCmPerMs_ * vx_pos) + 0.5f);

    triggerCm_  = trigger_cm;
    distanceCm_ = front_cm;

    // Convert vx → cm/s so the min-speed gate compares apples to apples.
    const uint16_t vx_cmps = (uint16_t)((vx_pos * 100.0f) + 0.5f);
    const bool moving_forward = vx_cmps >= minSpeedCmPs_;
    const bool obstacle_close = front_ok && front_cm <= trigger_cm;

    if (moving_forward && obstacle_close) {
        const uint32_t now = millis();
        if (!engaged_) {
            engaged_     = true;
            lastBrakeMs_ = now;
            drive_->brake();
            Serial.printf(
                "[autobrake] ENGAGED  front=%u cm  trigger=%u cm  vx=%.2f m/s\n",
                front_cm, trigger_cm, vx_ms);
        } else if (drive_->isMoving() && (now - lastBrakeMs_ >= kBrakeRepeatMs)) {
            // Still moving forward despite our brake — re-assert. This can
            // happen if a transport racy-spams `forward` faster than we
            // run, briefly pre-empting the active brake.
            drive_->brake();
            lastBrakeMs_ = now;
        }
    } else if (engaged_) {
        engaged_ = false;
        Serial.printf(
            "[autobrake] released  front=%u cm  vx=%.2f m/s\n",
            front_cm, vx_ms);
    }
}

}  // namespace smartrc
