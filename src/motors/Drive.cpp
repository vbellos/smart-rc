#include "motors/Drive.h"

#include <Arduino.h>
#include <math.h>

#include "sensors/Mpu6050.h"

namespace smartrc {

namespace {
// Minimum |vx| (m/s) considered "moving". Below this the brake is a no-op.
// Raised from 0.15 after field testing: integrator drift from sporadic
// accel spikes could accumulate ~0.25 m/s of ghost velocity on a parked
// car before ZUPT caught it. 0.30 leaves plenty of margin without losing
// real-world low-speed braking (the brake still catches any push that
// gets the car rolling at ~1 km/h or faster).
constexpr float kMotionThresholdMs = 0.30f;
}  // namespace

void Drive::begin(MotorDriver* driver, uint8_t defaultPwm) {
    driver_      = driver;
    defaultPwm_  = defaultPwm;
    lastMovement_ = Movement::Idle;
    lastMoveMs_   = 0;
    activeBraking_ = false;
    stop();
}

void Drive::forward(uint8_t speed) {
    if (!driver_) return;
    if (activeBraking_) cancelBrake();
    if (speed == 0) speed = defaultPwm_;
    driver_->set(MotorChannel::B,
                 inverted_ ? MotorDir::Reverse : MotorDir::Forward,
                 speed);
    moving_       = true;
    lastMovement_ = Movement::Forward;
    lastMoveMs_   = millis();
}

void Drive::reverse(uint8_t speed) {
    if (!driver_) return;
    if (activeBraking_) cancelBrake();
    if (speed == 0) speed = defaultPwm_;
    driver_->set(MotorChannel::B,
                 inverted_ ? MotorDir::Forward : MotorDir::Reverse,
                 speed);
    moving_       = true;
    lastMovement_ = Movement::Reverse;
    lastMoveMs_   = millis();
}

void Drive::stop() {
    if (!driver_) return;
    if (activeBraking_) cancelBrake();
    driver_->set(MotorChannel::B, MotorDir::Coast, 0);
    moving_ = false;
    // NOTE: we deliberately DON'T reset lastMovement_ here — a quick
    // "forward → release → brake" sequence (typical press-and-hold UX
    // on the app) should still active-brake because the car is still
    // rolling. kRecentMovementMs gates how long we keep this context.
}

void Drive::brake() {
    if (!driver_) return;
    if (activeBraking_) return;   // already braking; ignore repeat-tap

    // IMU is the source of truth for "is the car moving right now?". If
    // it's not present / not calibrated, we can't reason about motion —
    // leave the motor alone rather than guess.
    if (imu_ == nullptr || !imu_->present() || !imu_->gyroCalibrated()) {
        Serial.println("[drive] brake: IMU unavailable — no-op");
        postEvent("brake_noop_no_imu", 0);
        return;
    }

    const float v = imu_->velocityX();   // vehicle-forward, m/s (signed)

    // Double-gate: below the motion threshold OR ZUPT says we're at rest.
    // The ZUPT flag catches the case where vx has drifted slightly but
    // ax has been quiet for long enough that we're confidently stopped.
    if (fabsf(v) < kMotionThresholdMs || imu_->isStationary()) {
        Serial.printf("[drive] brake: |v|=%.2f m/s stationary=%d — no-op\n",
                      fabsf(v), (int)imu_->isStationary());
        postEvent("brake_noop_stopped", v);
        return;
    }

    // Apply opposing motor torque. v > 0 means car is heading "forward"
    // in vehicle frame → we need the motor in Reverse. The invert flag
    // swaps electrical polarity but NOT the logical direction we picked.
    const bool motionIsForward = (v > 0.0f);
    const MotorDir opposite = motionIsForward
        ? (inverted_ ? MotorDir::Forward : MotorDir::Reverse)
        : (inverted_ ? MotorDir::Reverse : MotorDir::Forward);
    driver_->set(MotorChannel::B, opposite, activeBrakePwm_);

    activeBraking_    = true;
    brakeStartMs_     = millis();
    stillCount_       = 0;
    brakeInitialSign_ = motionIsForward ? 1 : -1;
    moving_           = true;
    Serial.printf("[drive] active brake engaged (v=%+.2f m/s, sign=%+d)\n",
                  v, brakeInitialSign_);
    postEvent("brake_engaged", v);
}

void Drive::update() {
    if (!activeBraking_ || !driver_) return;

    const uint32_t elapsed = millis() - brakeStartMs_;

    // Hard timeout — always wins so we can never hang motor-reversed.
    if (elapsed > activeBrakeMaxMs_) {
        Serial.println("[drive] active brake: timeout");
        postEvent("brake_timeout", imu_ ? imu_->velocityX() : 0);
        finishBrake();
        return;
    }

    if (!imu_ || !imu_->present()) return;

    const float v = imu_->velocityX();
    const float a = imu_->last().ax;

    // 1) Sign-crossing: vx has passed through zero. The car has
    //    reached rest; stop NOW before the motor starts driving it the
    //    other way.
    if (brakeInitialSign_ != 0 && ((int)(v > 0.0f) - (int)(v < 0.0f))
        != brakeInitialSign_) {
        Serial.printf("[drive] active brake: vx crossed zero (%+.2f) in %lu ms\n",
                      v, (unsigned long)elapsed);
        postEvent("brake_settled", v);
        finishBrake();
        return;
    }

    // 2) Magnitude threshold / ZUPT — defensive: integrator could have
    //    missed the exact crossing if the brake was brief.
    if (fabsf(v) < kMotionThresholdMs || imu_->isStationary()) {
        Serial.printf("[drive] active brake: settled in %lu ms (v=%+.2f)\n",
                      (unsigned long)elapsed, v);
        postEvent("brake_settled", v);
        finishBrake();
        return;
    }

    // 3) SAFETY — if after ~150 ms the motor is accelerating the car in
    //    its current direction (i.e., sign(a) == sign(v) with enough
    //    magnitude to rule out noise), our direction guess was wrong
    //    (usually an IMU-invert misconfiguration). Kill the brake
    //    instead of pushing the car further.
    if (elapsed > 150 && fabsf(a) > 2.0f && (a * v) > 0.0f) {
        Serial.printf("[drive] active brake ABORTED: motor in wrong direction "
                      "(v=%+.2f, a=%+.2f) — check IMU invertX flag\n", v, a);
        postEvent("brake_aborted_wrong_dir", v, a);
        finishBrake();
        return;
    }
}

void Drive::finishBrake() {
    activeBraking_    = false;
    stillCount_       = 0;
    brakeInitialSign_ = 0;
    // Short-brake to hold the motor at rest (electromagnetic parking).
    driver_->set(MotorChannel::B, MotorDir::Brake, 0);
    moving_       = false;
    lastMovement_ = Movement::Idle;
    // Tell the integrator the car is at rest now. Motor-vibration noise
    // accumulated during the brake makes the residual vx unreliable —
    // snapping it back to zero is both more accurate than waiting for
    // ZUPT (1.5s) and prevents a cascading mis-brake on the next press.
    if (imu_) imu_->resetVelocity();
}

void Drive::cancelBrake() {
    // Silent abort — a new drive command is overriding. The caller will
    // immediately issue its own driver_->set(), so we don't need to
    // coast-set here.
    activeBraking_ = false;
    stillCount_    = 0;
}

void Drive::postEvent(const char* kind, float v, float a) {
    pendingEvent_     = true;
    pendingEventKind_ = kind;
    pendingEventV_    = v;
    pendingEventA_    = a;
    pendingEventTs_   = millis();
}

bool Drive::takeEvent(Event& out) {
    if (!pendingEvent_) return false;
    out.kind  = pendingEventKind_;
    out.v     = pendingEventV_;
    out.a     = pendingEventA_;
    out.ts_ms = pendingEventTs_;
    pendingEvent_ = false;
    return true;
}

}  // namespace smartrc
