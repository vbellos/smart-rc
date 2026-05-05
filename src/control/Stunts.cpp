#include "control/Stunts.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "config/Config.h"
#include "control/Safety.h"
#include "motors/Drive.h"
#include "motors/Steering.h"
#include "sensors/Mpu6050.h"

namespace smartrc {

namespace {
// Safety fallbacks when no Config is wired (shouldn't happen in practice
// — main.cpp always passes g_cfg — but keeps the engine usable if a
// test harness forgets).
constexpr uint16_t kDefaultSpinTargetDeg = 340;
constexpr uint16_t kDefaultSpinTimeoutMs = 2500;
constexpr uint8_t  kDefaultSpinPwm       = 255;
}  // namespace

using Step = StuntEngine::Step;
using K    = StuntEngine::StepKind;

// ===========================================================================
// Lifecycle
// ===========================================================================

void StuntEngine::begin(const Deps& deps) {
    deps_ = deps;
    active_ = StuntId::None;
}

const char* StuntEngine::nameFor(StuntId id) {
    switch (id) {
        case StuntId::SpinLeft:     return "spin_left";
        case StuntId::SpinRight:    return "spin_right";
        case StuntId::JTurnLeft:    return "j_turn_left";
        case StuntId::JTurnRight:   return "j_turn_right";
        case StuntId::Wiggle:       return "wiggle";
        case StuntId::DriftLeft:    return "drift_left";
        case StuntId::DriftRight:   return "drift_right";
        case StuntId::PowerReverse: return "power_reverse";
        default:                    return "none";
    }
}

const char* StuntEngine::currentName() const {
    return nameFor(active_);
}

bool StuntEngine::start(StuntId id) {
    if (!deps_.drive || !deps_.steering || !deps_.safety) return false;
    if (id == StuntId::None) return false;
    // Cancel any running stunt first (but silently — no abort event; a new
    // start supersedes the old one).
    if (active_ != StuntId::None) {
        deps_.drive->stop();
        deps_.steering->stop();
    }
    active_     = id;
    startedMs_  = millis();
    stepStartMs_ = startedMs_;
    stepIndex_   = 0;
    stepBufferLen_ = 0;
    yawAccum_    = 0.0f;
    spinLastTickMs_ = startedMs_;
    steerDesire_ = StepKind::SteerStop;   // fresh start: no steering held
    steerSpeed_  = 0;

    // Rebuild the step buffer from live config — so edits in Config ->
    // Stunts take effect on the NEXT stunt invocation, no reboot.
    switch (id) {
        case StuntId::JTurnLeft:    buildJTurn(true);      break;
        case StuntId::JTurnRight:   buildJTurn(false);     break;
        case StuntId::Wiggle:       buildWiggle();         break;
        case StuntId::DriftLeft:    buildDrift(true);      break;
        case StuntId::DriftRight:   buildDrift(false);     break;
        case StuntId::PowerReverse: buildPowerReverse();   break;
        case StuntId::SpinLeft:
        case StuntId::SpinRight:
        default: /* gyro-based, no buffer needed */ break;
    }

    postEvent("stunt_start");
    Serial.printf("[stunt] start: %s (%u steps)\n",
                  currentName(), (unsigned)stepBufferLen_);
    return true;
}

bool StuntEngine::startByName(const char* name) {
    if (!name) return false;
    if (!strcmp(name, "spin_left"))     return start(StuntId::SpinLeft);
    if (!strcmp(name, "spin_right"))    return start(StuntId::SpinRight);
    if (!strcmp(name, "j_turn_left"))   return start(StuntId::JTurnLeft);
    if (!strcmp(name, "j_turn_right"))  return start(StuntId::JTurnRight);
    if (!strcmp(name, "wiggle"))        return start(StuntId::Wiggle);
    if (!strcmp(name, "drift_left"))    return start(StuntId::DriftLeft);
    if (!strcmp(name, "drift_right"))   return start(StuntId::DriftRight);
    if (!strcmp(name, "power_reverse")) return start(StuntId::PowerReverse);
    return false;
}

void StuntEngine::abort() {
    if (active_ == StuntId::None) return;
    Serial.printf("[stunt] abort: %s\n", currentName());
    postEvent("stunt_abort");
    finish(/*aborted=*/true);
}

void StuntEngine::finish(bool aborted) {
    if (deps_.drive)    deps_.drive->stop();
    if (deps_.steering) deps_.steering->stop();
    steerDesire_ = StepKind::SteerStop;
    steerSpeed_  = 0;
    if (!aborted) {
        Serial.printf("[stunt] end: %s\n", currentName());
        postEvent("stunt_end");
    }
    active_ = StuntId::None;
}

// ===========================================================================
// Per-tick dispatch
// ===========================================================================

void StuntEngine::update() {
    if (active_ == StuntId::None || !deps_.drive || !deps_.steering || !deps_.safety) return;

    // Any latched emergency immediately kills the stunt.
    if (deps_.safety->isEmergency()) {
        abort();
        return;
    }

    // Every tick while a stunt runs counts as a command — keep the
    // safety watchdog fresh.
    deps_.safety->notifyHeartbeat();

    switch (active_) {
        case StuntId::SpinLeft:     tickSpin(true);  return;
        case StuntId::SpinRight:    tickSpin(false); return;
        default:                    tickSteppedStunt(); return;
    }
}

// ---------------------------------------------------------------------------
// Step-driven stunts
// ---------------------------------------------------------------------------

void StuntEngine::tickSteppedStunt() {
    if (stepIndex_ >= stepBufferLen_) { finish(/*aborted=*/false); return; }

    const Step& cur = stepBuffer_[stepIndex_];

    // First tick of a new step: issue the action (may set steerDesire_).
    const bool justEntered = (millis() == stepStartMs_) || (millis() - stepStartMs_ < 10);
    if (justEntered) applyStep(cur);

    // Refresh the persistent steering desire every tick — keeps the
    // steering pulse alive even across drive-only steps (e.g. the reverse
    // phase of a J-turn, where we want the wheels locked through the
    // whole 900 ms reverse burst).
    if (steerDesire_ == K::SteerLeft)  deps_.steering->pulseLeft(steerSpeed_);
    else if (steerDesire_ == K::SteerRight) deps_.steering->pulseRight(steerSpeed_);

    // Advance when hold time elapses (hold_ms=0 means "advance now").
    if (cur.hold_ms == 0 || (millis() - stepStartMs_) >= cur.hold_ms) {
        stepIndex_   += 1;
        stepStartMs_  = millis();
    }
}

void StuntEngine::applyStep(const Step& s) {
    switch (s.kind) {
        case K::Forward:    deps_.drive->forward(s.speed);       break;
        case K::Reverse:    deps_.drive->reverse(s.speed);       break;
        case K::Brake:      deps_.drive->brake();                break;
        case K::Stop:       deps_.drive->stop();                 break;
        // Steer actions set a persistent desire so the next tick + every
        // tick after keeps refreshing the pulse until another steer step
        // changes it.
        case K::SteerLeft:
            steerDesire_ = K::SteerLeft; steerSpeed_ = s.speed;
            deps_.steering->pulseLeft(s.speed);
            break;
        case K::SteerRight:
            steerDesire_ = K::SteerRight; steerSpeed_ = s.speed;
            deps_.steering->pulseRight(s.speed);
            break;
        case K::SteerStop:
            steerDesire_ = K::SteerStop; steerSpeed_ = 0;
            deps_.steering->stop();
            break;
    }
}

// ---------------------------------------------------------------------------
// Gyro-feedback spin
// ---------------------------------------------------------------------------

void StuntEngine::tickSpin(bool left) {
    const Config* c = deps_.config;
    const uint8_t  pwm       = c ? c->stuntSpinPwm       : kDefaultSpinPwm;
    const uint16_t targetDeg = c ? c->stuntSpinTargetDeg : kDefaultSpinTargetDeg;
    const uint16_t timeoutMs = c ? c->stuntSpinTimeoutMs : kDefaultSpinTimeoutMs;

    // Refresh motor state every tick — Steering pulses need it, and the
    // drive motor is a cheap re-set.
    if (left) deps_.steering->pulseLeft(255);
    else      deps_.steering->pulseRight(255);
    deps_.drive->forward(pwm);

    // Integrate yaw rate (gz) if we have an IMU. Otherwise fall back to
    // a pure timeout.
    const uint32_t now = millis();
    const uint32_t dtMs = now - spinLastTickMs_;
    spinLastTickMs_ = now;

    if (deps_.imu && deps_.imu->present() && dtMs < 200) {
        const float dt = dtMs / 1000.0f;
        const float gz = deps_.imu->last().gz;  // °/s, bias-corrected
        yawAccum_ += fabsf(gz) * dt;
        if (yawAccum_ >= (float)targetDeg) {
            Serial.printf("[stunt] spin done (yaw=%.1f°, %lu ms)\n",
                          yawAccum_, (unsigned long)(now - startedMs_));
            finish(/*aborted=*/false);
            return;
        }
    }

    // Timeout safety net (for absent/bad IMU or a car that can't rotate).
    if ((now - startedMs_) >= timeoutMs) {
        Serial.printf("[stunt] spin timeout (yaw=%.1f°)\n", yawAccum_);
        finish(/*aborted=*/false);
    }
}

// ---------------------------------------------------------------------------
// Runtime step builders — pull all values from deps_.config so edits in
// /api/config take effect on the next stunt invocation with no reboot.
// ---------------------------------------------------------------------------

void StuntEngine::buildJTurn(bool left) {
    const Config* c = deps_.config;
    const uint8_t  pwm     = c ? c->stuntJturnPwm     : 255;
    const uint16_t fwdMs   = c ? c->stuntJturnFwdMs   : 700;
    const uint16_t brakeMs = c ? c->stuntJturnBrakeMs : 300;
    const uint16_t revMs   = c ? c->stuntJturnRevMs   : 900;
    size_t i = 0;
    stepBuffer_[i++] = { K::Forward, pwm, fwdMs };
    stepBuffer_[i++] = { left ? K::SteerLeft : K::SteerRight, 255, 0 };
    stepBuffer_[i++] = { K::Brake, 0, brakeMs };
    stepBuffer_[i++] = { K::Reverse, pwm, revMs };
    stepBuffer_[i++] = { K::SteerStop, 0, 0 };
    stepBuffer_[i++] = { K::Stop, 0, 0 };
    stepBufferLen_ = i;
}

void StuntEngine::buildWiggle() {
    const Config* c = deps_.config;
    const uint8_t  pwm    = c ? c->stuntWigglePwm    : 255;
    const uint16_t kickMs = c ? c->stuntWiggleKickMs : 150;
    const uint16_t holdMs = c ? c->stuntWiggleHoldMs : 280;
    const uint8_t  cycles = c ? c->stuntWiggleCycles : 3;
    size_t i = 0;
    stepBuffer_[i++] = { K::Forward, pwm, kickMs };
    const uint8_t clampedCycles = cycles > 8 ? 8 : (cycles < 1 ? 1 : cycles);
    for (uint8_t n = 0; n < clampedCycles && i + 3 < kMaxSteps; ++n) {
        stepBuffer_[i++] = { K::SteerLeft,  255, holdMs };
        stepBuffer_[i++] = { K::SteerRight, 255, holdMs };
    }
    stepBuffer_[i++] = { K::SteerStop, 0, 0 };
    stepBuffer_[i++] = { K::Stop, 0, 0 };
    stepBufferLen_ = i;
}

void StuntEngine::buildDrift(bool left) {
    const Config* c = deps_.config;
    const uint8_t  pwm     = c ? c->stuntDriftPwm        : 255;
    const uint16_t fwd1Ms  = c ? c->stuntDriftFwd1Ms     : 300;
    const uint16_t lockMs  = c ? c->stuntDriftLockMs     : 400;
    const uint16_t ctrMs   = c ? c->stuntDriftCounterMs  : 300;
    size_t i = 0;
    stepBuffer_[i++] = { K::Forward, pwm, fwd1Ms };
    stepBuffer_[i++] = { left ? K::SteerLeft : K::SteerRight, 255, 0 };
    stepBuffer_[i++] = { K::Forward, pwm, lockMs };
    stepBuffer_[i++] = { left ? K::SteerRight : K::SteerLeft, 255, 0 };
    stepBuffer_[i++] = { K::Forward, pwm, ctrMs };
    stepBuffer_[i++] = { K::SteerStop, 0, 0 };
    stepBuffer_[i++] = { K::Stop, 0, 0 };
    stepBufferLen_ = i;
}

void StuntEngine::buildPowerReverse() {
    const Config* c = deps_.config;
    const uint8_t  pwm     = c ? c->stuntPwrRevPwm     : 255;
    const uint16_t fwdMs   = c ? c->stuntPwrRevFwdMs   : 600;
    const uint16_t brakeMs = c ? c->stuntPwrRevBrakeMs : 250;
    const uint16_t revMs   = c ? c->stuntPwrRevRevMs   : 700;
    size_t i = 0;
    stepBuffer_[i++] = { K::Forward, pwm, fwdMs };
    stepBuffer_[i++] = { K::Brake, 0, brakeMs };
    stepBuffer_[i++] = { K::Reverse, pwm, revMs };
    stepBuffer_[i++] = { K::Stop, 0, 0 };
    stepBufferLen_ = i;
}

// ---------------------------------------------------------------------------
// Event queue
// ---------------------------------------------------------------------------

void StuntEngine::postEvent(const char* kind) {
    pendingEvent_.kind  = kind;
    pendingEvent_.name  = currentName();
    pendingEvent_.ts_ms = millis();
    hasEvent_ = true;
}

bool StuntEngine::takeEvent(Event& out) {
    if (!hasEvent_) return false;
    out = pendingEvent_;
    hasEvent_ = false;
    return true;
}

}  // namespace smartrc
