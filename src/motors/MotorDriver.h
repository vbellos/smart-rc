#pragma once

#include <stdint.h>

namespace smartrc {

// Logical channels of the TB6612.
enum class MotorChannel : uint8_t { A = 0, B = 1 };

// Direction request for a single channel.
enum class MotorDir : uint8_t { Coast, Forward, Reverse, Brake };

// Low-level driver: configures the six TB6612 GPIOs (PWM + 2 dir per channel)
// and exposes a uniform set(channel, dir, speed) API. Knows nothing about
// "drive" vs "steering" — that's layered above.
class MotorDriver {
public:
    void begin();

    // Drive a channel. `speed` is 0..255. Brake / Coast ignore speed.
    void set(MotorChannel ch, MotorDir dir, uint8_t speed);

    // Convenience: stop both channels with a hard brake.
    void stopAll();
};

}  // namespace smartrc
