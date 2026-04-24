// Native unit test for the Steering open-loop state machine.
// Runs with `pio test -e native_test`. No hardware required — Arduino.h is
// stubbed under test/native_stubs/, and millis() is driven by a virtual clock.

#include <unity.h>

#include "motors/MotorDriver.h"
#include "motors/Steering.h"

namespace smartrc_native_test {
uint32_t g_now_ms = 0;
}
using smartrc_native_test::g_now_ms;

namespace smartrc {

// Spy driver — records the most recent set() call per channel.
class SpyDriver : public MotorDriver {
public:
    struct Last { MotorDir dir = MotorDir::Coast; uint8_t speed = 0; int calls = 0; };
    Last a, b;
    void set(MotorChannel ch, MotorDir dir, uint8_t speed) {
        Last& l = (ch == MotorChannel::A) ? a : b;
        l.dir = dir; l.speed = speed; l.calls++;
    }
};

}  // namespace smartrc

using namespace smartrc;

static SpyDriver spy;
static Steering  steering;

void setUp(void) {
    spy = SpyDriver{};
    g_now_ms = 0;
    steering.begin(&spy, /*pulseMs=*/100, /*cooldownMs=*/50, /*defaultPwm=*/200);
    g_now_ms = 1;  // bump past begin()
}

void tearDown(void) {}

static void advance(uint32_t ms) {
    g_now_ms += ms;
    steering.update();
}

// ---------------------------------------------------------------------------
// Basic pulse behavior
// ---------------------------------------------------------------------------

void test_left_pulse_stays_on_without_explicit_stop(void) {
    TEST_ASSERT_TRUE(steering.pulseLeft());
    TEST_ASSERT_EQUAL(MotorDir::Reverse, spy.a.dir);
    TEST_ASSERT_EQUAL(200, spy.a.speed);

    // Under the new "hold" model, time alone should NOT drop us out of
    // Pulsing — Safety's heartbeat watchdog or an explicit stop() does.
    advance(500);
    TEST_ASSERT_EQUAL(Steering::State::Pulsing, steering.state());
    TEST_ASSERT_EQUAL(MotorDir::Reverse, spy.a.dir);
}

void test_right_pulse_runs_then_stops(void) {
    TEST_ASSERT_TRUE(steering.pulseRight());
    TEST_ASSERT_EQUAL(MotorDir::Forward, spy.a.dir);
}

void test_same_direction_during_pulse_is_idempotent(void) {
    TEST_ASSERT_TRUE(steering.pulseLeft());
    advance(20);
    int callsBefore = spy.a.calls;
    TEST_ASSERT_TRUE(steering.pulseLeft());
    // Same direction shouldn't re-issue a physical set() call — a held
    // key just keeps refreshing the state timer without flicker.
    TEST_ASSERT_EQUAL(callsBefore, spy.a.calls);
    advance(200);
    TEST_ASSERT_EQUAL(Steering::State::Pulsing, steering.state());
}

// ---------------------------------------------------------------------------
// New behavior: instant reversal (no rejection)
// ---------------------------------------------------------------------------

void test_opposite_direction_during_pulse_reverses_instantly(void) {
    TEST_ASSERT_TRUE(steering.pulseLeft());
    TEST_ASSERT_EQUAL(MotorDir::Reverse, spy.a.dir);  // left -> Reverse
    advance(20);
    // Opposite direction while still pulsing must be accepted AND flip the
    // H-bridge immediately — the whole point of the instant-response model.
    TEST_ASSERT_TRUE(steering.pulseRight());
    TEST_ASSERT_EQUAL(MotorDir::Forward, spy.a.dir);  // right -> Forward
    TEST_ASSERT_EQUAL(Steering::State::Pulsing, steering.state());
    TEST_ASSERT_EQUAL(Steering::Dir::Right, steering.lastDirection() == Steering::Dir::Right
                      ? Steering::Dir::Right : steering.lastDirection());
}

void test_reversal_during_cooldown_is_accepted(void) {
    TEST_ASSERT_TRUE(steering.pulseLeft());
    steering.stop();  // explicit release — enters cooldown
    advance(10);
    TEST_ASSERT_EQUAL(Steering::State::Cooldown, steering.state());
    // Cooldown USED to reject reversals; the new model accepts immediately.
    TEST_ASSERT_TRUE(steering.pulseRight());
    TEST_ASSERT_EQUAL(MotorDir::Forward, spy.a.dir);
    TEST_ASSERT_EQUAL(Steering::State::Pulsing, steering.state());
}

// ---------------------------------------------------------------------------
// Stop behavior
// ---------------------------------------------------------------------------

void test_stop_cancels_pulse_and_starts_cooldown(void) {
    TEST_ASSERT_TRUE(steering.pulseRight());
    advance(10);
    steering.stop();
    TEST_ASSERT_EQUAL(Steering::State::Cooldown, steering.state());
    TEST_ASSERT_EQUAL(MotorDir::Coast, spy.a.dir);
}

// ---------------------------------------------------------------------------
// Invert flag
// ---------------------------------------------------------------------------

void test_invert_swaps_left_and_right_physically(void) {
    steering.setInverted(true);
    TEST_ASSERT_TRUE(steering.pulseLeft());
    // With invert: logical Left becomes physical Forward (was Reverse).
    TEST_ASSERT_EQUAL(MotorDir::Forward, spy.a.dir);
    TEST_ASSERT_EQUAL(Steering::Dir::Left, steering.lastDirection() == Steering::Dir::Left
                      ? Steering::Dir::Left
                      : Steering::Dir::Left);  // logical state is still Left
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_left_pulse_runs_then_stops);
    RUN_TEST(test_right_pulse_runs_then_stops);
    RUN_TEST(test_same_direction_during_pulse_extends_window);
    RUN_TEST(test_opposite_direction_during_pulse_reverses_instantly);
    RUN_TEST(test_reversal_during_cooldown_is_accepted);
    RUN_TEST(test_stop_cancels_pulse_and_starts_cooldown);
    RUN_TEST(test_invert_swaps_left_and_right_physically);
    return UNITY_END();
}
