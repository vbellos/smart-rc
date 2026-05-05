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

// Continuous-ranging period. 20 Hz is plenty for obstacle avoidance and
// keeps the I²C bandwidth share modest alongside the 50 Hz MPU6050.
constexpr uint16_t kReadIntervalMs = 50;

// Pololu library's I/O timeout. Generous on purpose: with the
// non-blocking dataReady-gated read pattern below we should never hit
// it; this is just a safety net against a stuck sensor wedging the bus.
constexpr uint16_t kIoTimeoutMs    = 200;

// VL53L0X result-interrupt-status register. Bits 2:0 carry the
// data-ready / range-status flag — non-zero means a fresh sample is
// waiting. We poll this ourselves to keep the read non-blocking; the
// pololu library's readRangeContinuousMillimeters() would tight-loop on
// the same register and stall loop() while waiting.
constexpr uint8_t REG_RESULT_INTERRUPT_STATUS = 0x13;

// Consecutive failures before we hard-reset (XSHUT cycle + re-init) the
// sensor. ~8 misses = ~0.4 s at 20 Hz, long enough to ride out a
// transient bus glitch but short enough to recover before the user
// notices a long drop-out.
constexpr uint8_t kRecoverAfterFails = 8;

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

        ::VL53L0X& drv = g_drv[i];

        // Non-blocking dataReady probe. If the sample isn't ready yet,
        // skip — keeps loop() snappy. The pololu library's blocking
        // readRangeContinuousMillimeters() would tight-poll this same
        // register and stall the whole loop for tens of ms, which is
        // what was making the sensor "feel" slow / disconnected.
        const uint8_t status = drv.readReg(REG_RESULT_INTERRUPT_STATUS);

        // last_status is non-zero on any I²C error (NACK, timeout, bus
        // wedge). Treat as a soft failure — we'll recover after enough
        // consecutive misses.
        if (drv.last_status != 0) {
            state_[i].valid = false;
            if (++state_[i].failCount >= kRecoverAfterFails) recover(i);
            continue;
        }

        // Bits 2:0 carry the data-ready flag. Zero = no fresh sample yet
        // (we polled before the integration window finished); just hold
        // off and try again next tick. Don't bump failCount — this is
        // the *normal* idle-tick outcome.
        if ((status & 0x07) == 0) continue;

        // Sample is ready: read it. The library still polls internally,
        // but with the data-ready bit already set it returns essentially
        // immediately (1-2 register reads, no waiting).
        const uint16_t mm = drv.readRangeContinuousMillimeters();

        if (drv.timeoutOccurred() || drv.last_status != 0) {
            state_[i].valid = false;
            if (++state_[i].failCount >= kRecoverAfterFails) recover(i);
            continue;
        }

        state_[i].mm        = mm;
        // 8190 mm is the library's "out of range" sentinel; everything
        // beyond that is noise on a VL53L0X.
        state_[i].valid     = (mm < 8190);
        state_[i].failCount = 0;
    }
}

void DistanceSensors::recover(uint8_t i) {
    const SlotConfig& cfg = kSlots[i];
    Serial.printf("[tof] %s sensor recovering (%u consecutive failures)...\n",
                  cfg.label, state_[i].failCount);

    // XSHUT-cycle just this sensor — the other one keeps running on its
    // already-assigned address and is unaffected.
    digitalWrite(cfg.xshut_pin, LOW);
    delay(10);
    digitalWrite(cfg.xshut_pin, HIGH);
    delay(10);

    ::VL53L0X& drv = g_drv[i];
    drv.setTimeout(kIoTimeoutMs);

    if (!drv.init()) {
        Serial.printf("[tof] %s sensor recovery failed — marking absent\n",
                      cfg.label);
        state_[i].present = false;
        state_[i].valid   = false;
        return;
    }

    drv.setAddress(cfg.target_addr);
    drv.startContinuous(kReadIntervalMs);
    state_[i].failCount = 0;
    state_[i].valid     = false;  // first reading after re-init isn't ready yet
    Serial.printf("[tof] %s sensor recovered @ 0x%02X\n",
                  cfg.label, cfg.target_addr);
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
