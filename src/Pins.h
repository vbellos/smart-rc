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

// I²C bus shared by any sensor peripherals (MPU6050, future distance /
// battery / power-monitor chips). Pulled-up on the breakout boards, so
// no external pullups needed.
constexpr uint8_t  I2C_SDA       = 10;
constexpr uint8_t  I2C_SCL       = 11;
constexpr uint32_t I2C_FREQ_HZ   = 400000;  // fast-mode; drop to 100000 if flaky

// MPU6050 IMU — default address when AD0 is tied to GND.
constexpr uint8_t  MPU6050_ADDR  = 0x68;

// VL53L0X time-of-flight distance sensors.
// All VL53L0X chips boot at I²C 0x29, so multiple sensors on one bus need
// their XSHUT pins on separate GPIOs: at boot we hold every sensor in
// reset, then bring them up one at a time and reassign each to a unique
// address. Even with a single sensor populated, XSHUT must be on a GPIO
// (not strapped HIGH) so the dance still works when the second sensor is
// added later without re-routing the harness.
constexpr uint8_t  TOF_FRONT_XSHUT = 12;
constexpr uint8_t  TOF_REAR_XSHUT  = 13;
constexpr uint8_t  TOF_FRONT_ADDR  = 0x30;
constexpr uint8_t  TOF_REAR_ADDR   = 0x31;

}  // namespace pins
