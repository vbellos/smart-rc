#include "sensors/DistanceSensors.h"

#include <Arduino.h>
#include <VL53L0X.h>

#include "Pins.h"

namespace smartrc {
namespace sensors {

namespace {

struct SlotConfig {
    uint8_t     xshut_pin;
    uint8_t     target_addr;
    const char* label;
};

constexpr SlotConfig kSlots[DistanceSensors::kCount] = {
    { pins::TOF_FRONT_XSHUT, pins::TOF_FRONT_ADDR, "front" },
    { pins::TOF_REAR_XSHUT,  pins::TOF_REAR_ADDR,  "rear"  },
};

// One driver instance per slot. Kept in the .cpp to avoid leaking the
// pololu library header into DistanceSensors.h.
::VL53L0X g_drv[DistanceSensors::kCount];

// Continuous-ranging period. ~30 Hz is plenty for an obstacle-detection
// driving aid and matches the bus headroom we have alongside the IMU.
constexpr uint16_t kReadIntervalMs = 33;

// Bound how long readRangeContinuousMillimeters() may block if a fresh
// sample isn't ready yet — keeps loop() responsive when the sensor is
// momentarily slow.
constexpr uint16_t kIoTimeoutMs    = 50;

}  // namespace

void DistanceSensors::begin() {
    // Step 1: hold every sensor in reset. Doing this for both slots before
    // probing is what makes the address-reassignment safe — even if the
    // rear sensor isn't populated, driving its (floating) GPIO low costs
    // nothing.
    for (uint8_t i = 0; i < kCount; ++i) {
        pinMode(kSlots[i].xshut_pin, OUTPUT);
        digitalWrite(kSlots[i].xshut_pin, LOW);
    }
    delay(10);

    // Step 2: bring up each sensor in turn, reassign its address.
    for (uint8_t i = 0; i < kCount; ++i) {
        const SlotConfig& cfg = kSlots[i];

        digitalWrite(cfg.xshut_pin, HIGH);
        delay(10);  // datasheet boot time is ~1.2 ms; 10 ms is generous.

        ::VL53L0X& drv = g_drv[i];
        drv.setTimeout(kIoTimeoutMs);

        if (!drv.init()) {
            Serial.printf("[tof] %s sensor not detected on XSHUT GPIO %u\n",
                          cfg.label, cfg.xshut_pin);
            // Leave XSHUT high — sensor (if hot-plugged) will sit idle at
            // 0x29 and won't conflict because no one else uses that addr.
            state_[i].present = false;
            continue;
        }

        drv.setAddress(cfg.target_addr);
        drv.startContinuous(kReadIntervalMs);

        state_[i].present = true;
        Serial.printf("[tof] %s sensor initialised @ 0x%02X (%u Hz)\n",
                      cfg.label, cfg.target_addr,
                      1000u / kReadIntervalMs);
    }
}

void DistanceSensors::update() {
    const uint32_t now = millis();
    if (now - lastReadMs_ < kReadIntervalMs) return;
    lastReadMs_ = now;

    for (uint8_t i = 0; i < kCount; ++i) {
        if (!state_[i].present) continue;

        const uint16_t mm = g_drv[i].readRangeContinuousMillimeters();

        if (g_drv[i].timeoutOccurred()) {
            state_[i].valid = false;
            // Leave the previous mm reading visible — clients can decide
            // whether to ignore based on `valid`.
            continue;
        }

        state_[i].mm    = mm;
        // 8190 mm is the library's "out of range" sentinel; everything
        // beyond that is noise on a VL53L0X.
        state_[i].valid = (mm < 8190);
    }
}

bool DistanceSensors::present(Slot s) const {
    return s < kCount && state_[s].present;
}

uint16_t DistanceSensors::lastMm(Slot s) const {
    return s < kCount ? state_[s].mm : 0xFFFF;
}

bool DistanceSensors::valid(Slot s) const {
    return s < kCount && state_[s].valid;
}

void DistanceSensors::appendJson(JsonObject parent) const {
    // Wire shape: each slot is a plain number in **cm** (matches the
    // Monitor page chart unit) or `null` when absent / out-of-range. The
    // React app's distance card pulls `distance.front` directly as a
    // number — a nested object would break its `num()` accessor and the
    // chart would silently render empty.
    JsonObject d = parent["distance"].to<JsonObject>();
    for (uint8_t i = 0; i < kCount; ++i) {
        const char* key = kSlots[i].label;
        if (!state_[i].present || !state_[i].valid) {
            d[key] = nullptr;
            continue;
        }
        d[key] = state_[i].mm / 10;  // mm → cm
    }
}

}  // namespace sensors
}  // namespace smartrc
