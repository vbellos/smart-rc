#pragma once

#include <stdint.h>

namespace pins {

// TB6612 — fixed wiring per project spec.
// VCC <- 3V3, STBY <- 3V3, GND <- GND, VM <- external motor supply.
constexpr uint8_t PWMA = 4;   // steering PWM
constexpr uint8_t AIN1 = 5;   // steering dir 1
constexpr uint8_t AIN2 = 6;   // steering dir 2
constexpr uint8_t PWMB = 7;   // drive PWM
constexpr uint8_t BIN1 = 8;   // drive dir 1
constexpr uint8_t BIN2 = 9;   // drive dir 2

// PWM characteristics for LEDC.
constexpr uint32_t PWM_FREQ_HZ   = 20000; // above audible range
constexpr uint8_t  PWM_RESOLUTION = 8;     // 0..255
constexpr uint16_t PWM_MAX        = 255;

}  // namespace pins
