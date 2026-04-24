#include "motors/MotorDriver.h"

#include <Arduino.h>

#include "Pins.h"

namespace smartrc {

namespace {
struct ChannelPins {
    uint8_t pwm, in1, in2;
};

constexpr ChannelPins CH_PINS[2] = {
    { pins::PWMA, pins::AIN1, pins::AIN2 },  // A — steering
    { pins::PWMB, pins::BIN1, pins::BIN2 },  // B — drive
};
}  // namespace

void MotorDriver::begin() {
    for (const auto& cp : CH_PINS) {
        pinMode(cp.in1, OUTPUT);
        pinMode(cp.in2, OUTPUT);
        digitalWrite(cp.in1, LOW);
        digitalWrite(cp.in2, LOW);
        // Arduino-ESP32 3.x LEDC API: per-pin attach.
        ledcAttach(cp.pwm, pins::PWM_FREQ_HZ, pins::PWM_RESOLUTION);
        ledcWrite(cp.pwm, 0);
    }
}

void MotorDriver::set(MotorChannel ch, MotorDir dir, uint8_t speed) {
    const auto& cp = CH_PINS[static_cast<uint8_t>(ch)];

    switch (dir) {
        case MotorDir::Forward:
            digitalWrite(cp.in1, HIGH);
            digitalWrite(cp.in2, LOW);
            ledcWrite(cp.pwm, speed);
            break;
        case MotorDir::Reverse:
            digitalWrite(cp.in1, LOW);
            digitalWrite(cp.in2, HIGH);
            ledcWrite(cp.pwm, speed);
            break;
        case MotorDir::Brake:
            // Short-brake: both inputs high, PWM full.
            digitalWrite(cp.in1, HIGH);
            digitalWrite(cp.in2, HIGH);
            ledcWrite(cp.pwm, pins::PWM_MAX);
            break;
        case MotorDir::Coast:
        default:
            digitalWrite(cp.in1, LOW);
            digitalWrite(cp.in2, LOW);
            ledcWrite(cp.pwm, 0);
            break;
    }
}

void MotorDriver::stopAll() {
    set(MotorChannel::A, MotorDir::Brake, 0);
    set(MotorChannel::B, MotorDir::Brake, 0);
}

}  // namespace smartrc
