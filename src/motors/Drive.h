#pragma once

#include <stdint.h>

#include "motors/MotorDriver.h"

namespace smartrc {

namespace sensors { class Mpu6050; }

// Rear motor (TB6612 channel B): forward / reverse / stop / brake.
//
// `brake()` is an ACTIVE brake: if the car was recently driving, the
// H-bridge is reversed at `activeBrakePwm_` until either the IMU sees the
// vehicle-forward acceleration settle back near zero (N consecutive
// samples below threshold → "stopped") or a hard `activeBrakeMaxMs_`
// timeout trips. Call setImu(&mpu) once at boot; without an IMU the
// brake falls back to a time-limited reverse burst.
class Drive {
public:
    void begin(MotorDriver* driver, uint8_t defaultPwm);

    void forward(uint8_t speed = 0);
    void reverse(uint8_t speed = 0);
    void stop();
    void brake();

    // Tick from loop(). Cheap when idle; only does work while active
    // brake is engaged.
    void update();

    void setDefaultPwm(uint8_t pwm) { defaultPwm_ = pwm; }
    uint8_t defaultPwm() const { return defaultPwm_; }

    void setInverted(bool inverted) { inverted_ = inverted; }
    bool inverted() const { return inverted_; }

    // Active-brake tuning (both NVS-persisted via Config).
    void setActiveBrakePwm(uint8_t pwm)       { activeBrakePwm_   = pwm; }
    void setActiveBrakeMaxMs(uint16_t ms)     { activeBrakeMaxMs_ = ms; }

    // Plug in the IMU for motion-aware stop detection. Safe to pass
    // nullptr — brake degrades to a fixed-time reverse burst. Non-const
    // because finishBrake() pokes vx back to zero after a brake lands.
    void setImu(sensors::Mpu6050* imu) { imu_ = imu; }

    bool isMoving() const       { return moving_; }
    bool isActiveBraking() const { return activeBraking_; }

    // Single-slot event queue for WsServer to broadcast. Returns and
    // clears the last brake decision if any is pending. kind is one of:
    // "brake_engaged", "brake_settled", "brake_aborted_wrong_dir",
    // "brake_timeout", "brake_noop_stopped", "brake_noop_no_imu".
    struct Event { const char* kind; float v; float a; uint32_t ts_ms; };
    bool takeEvent(Event& out);

private:
    enum class Movement : uint8_t { Idle, Forward, Reverse };

    void finishBrake();          // release active brake, transition to short-brake
    void cancelBrake();           // abort active brake silently (new drive cmd)

    MotorDriver*              driver_         = nullptr;
    sensors::Mpu6050*         imu_            = nullptr;

    uint8_t  defaultPwm_       = 0;
    bool     moving_           = false;
    bool     inverted_         = false;

    // Last-commanded direction + timestamp — used to know which way to
    // actively brake when the car was recently driving.
    Movement lastMovement_     = Movement::Idle;
    uint32_t lastMoveMs_       = 0;

    // Active-brake state machine
    bool     activeBraking_    = false;
    uint32_t brakeStartMs_     = 0;
    uint8_t  stillCount_       = 0;
    int8_t   brakeInitialSign_ = 0;   // sign of vx when brake was engaged

    // Tunables
    uint8_t  activeBrakePwm_   = 220;
    uint16_t activeBrakeMaxMs_ = 600;

    // Pending brake event for external observation (WS broadcast).
    bool        pendingEvent_      = false;
    const char* pendingEventKind_  = nullptr;
    float       pendingEventV_     = 0;
    float       pendingEventA_     = 0;
    uint32_t    pendingEventTs_    = 0;

    void postEvent(const char* kind, float v, float a = 0);
};

}  // namespace smartrc
