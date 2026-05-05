#pragma once

#include <stdint.h>
#include <stddef.h>

namespace smartrc {

struct Config;
class Drive;
class Steering;
class Safety;
namespace sensors { class Mpu6050; }

// Scripted multi-step maneuvers ("stunts") — spin, J-turn, wiggle, drift,
// power-reverse. Each stunt is a short sequence of drive/steering actions
// plus timing; some also use the gyro for feedback (e.g. spin uses gz
// integration to stop at ~360°).
//
// Stunts talk to Drive + Steering + Safety directly (not via
// CommandHandler) so they can refresh the steering pulse every tick.
// Any external command through CommandHandler aborts an active stunt —
// user input always wins.

enum class StuntId : uint8_t {
    None = 0,
    SpinLeft,
    SpinRight,
    JTurnLeft,
    JTurnRight,
    Wiggle,
    DriftLeft,
    DriftRight,
    PowerReverse,
};

class StuntEngine {
public:
    struct Deps {
        Drive*                      drive;
        Steering*                   steering;
        Safety*                     safety;
        const sensors::Mpu6050*     imu;       // optional — gyro stunts
        const Config*               config;    // for stunt tunables
    };

    void begin(const Deps& deps);
    void update();

    // Start a stunt by id or by name ("spin_left", etc.). Returns true
    // if the stunt was recognised and started (cancelling any running
    // one); false if name is unknown or deps aren't set up yet.
    bool start(StuntId id);
    bool startByName(const char* name);

    // Cancel immediately. Motors returned to stop + steer-stop.
    void abort();

    bool isRunning() const { return active_ != StuntId::None; }
    StuntId current() const { return active_; }
    const char* currentName() const;

    // Number of completed + aborted events, for UI visibility.
    static const char* nameFor(StuntId id);

    // --- Single-slot event queue (consumed by WsServer to broadcast) ---
    struct Event {
        const char* kind;    // "stunt_start" | "stunt_end" | "stunt_abort"
        const char* name;    // stunt name
        uint32_t    ts_ms;
    };
    bool takeEvent(Event& out);

    // Step-based stunts: an array of these is walked, one step per
    // transition. `hold_ms == 0` means "instantaneous action then advance".
    // Public so the .cpp can declare const step arrays at namespace scope.
    enum class StepKind : uint8_t {
        Forward,
        Reverse,
        Brake,
        Stop,
        SteerLeft,
        SteerRight,
        SteerStop,
    };
    struct Step {
        StepKind kind;
        uint8_t  speed;    // 0 = default for drive; 0 for steer = default steer PWM
        uint16_t hold_ms;  // how long to stay on this step before advancing
    };

private:
    // Tick helpers
    void tickSteppedStunt();                   // walks stepBuffer_
    void tickSpin(bool left);                  // gyro-feedback spin
    void applyStep(const Step& s);

    // Runtime step builders — pull durations/PWMs from deps_.config.
    void buildJTurn(bool left);
    void buildWiggle();
    void buildDrift(bool left);
    void buildPowerReverse();

    void postEvent(const char* kind);

    void finish(bool aborted);

    Deps        deps_{};
    StuntId     active_      = StuntId::None;
    uint32_t    startedMs_   = 0;
    uint32_t    stepStartMs_ = 0;
    size_t      stepIndex_   = 0;

    // Runtime-built step sequence (fresh per start()). Sized to fit the
    // largest possible stunt — wiggle with 8 cycles = 1 kick + 16 steer
    // + 2 terminators = 19 steps; budget a bit extra.
    static constexpr size_t kMaxSteps = 24;
    Step    stepBuffer_[kMaxSteps] = {};
    size_t  stepBufferLen_          = 0;

    // Gyro-feedback state (spin stunts)
    float       yawAccum_    = 0.0f;    // degrees of accumulated rotation
    uint32_t    spinLastTickMs_ = 0;

    // Persistent steering desire — survives step transitions so drive-only
    // steps (Forward/Reverse/Brake) don't let the steering pulse lapse.
    // Reset on start() / finish(). Applied every tick while non-Stop.
    StepKind    steerDesire_ = StepKind::SteerStop;
    uint8_t     steerSpeed_  = 0;

    // Event queue
    Event       pendingEvent_{};
    bool        hasEvent_    = false;
};

}  // namespace smartrc
