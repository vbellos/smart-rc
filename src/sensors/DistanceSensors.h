#pragma once

#include <stdint.h>

#include <ArduinoJson.h>

namespace smartrc {
namespace sensors {

// VL53L0X cluster (front + rear). Each VL53L0X chip boots at I²C 0x29, so
// running multiple on one bus requires the XSHUT-dance: at begin() we hold
// every sensor in reset, then bring them up one at a time and move each
// to a unique address. Each slot is independently tolerated-absent — if a
// sensor doesn't ACK, it's marked not-present and the rest still work.
class DistanceSensors {
public:
    enum Slot : uint8_t { Front = 0, Rear = 1, kCount = 2 };

    /** Run the XSHUT-dance and start continuous ranging on every sensor
     *  that ACKs. Wire.begin() must already have been called. */
    void begin();

    /** Pump fresh samples. Rate-limited internally; cheap to call every
     *  loop(). */
    void update();

    bool     present(Slot s) const;
    /** Last reading in mm. 0xFFFF if no sample yet or sensor absent. */
    uint16_t lastMm(Slot s)  const;
    /** True when the last reading was in-range (not a timeout / saturation). */
    bool     valid(Slot s)   const;

    /** Append `parent["distance"]` with a per-slot object (or null when
     *  the slot is unpopulated). Called from sensors::appendSensorsJson(). */
    void appendJson(JsonObject parent) const;

private:
    struct State {
        bool     present    = false;
        bool     valid      = false;
        uint16_t mm         = 0xFFFF;
        // Consecutive read failures (timeout or I2C error). After a
        // threshold we XSHUT-cycle the sensor and re-init to recover from
        // a soft hang without rebooting the whole MCU.
        uint8_t  failCount  = 0;
    };
    State    state_[kCount] = {};
    uint32_t lastReadMs_    = 0;

    // XSHUT-cycle + re-init slot `i`. Called when failCount crosses the
    // recovery threshold. Logs and updates `present` accordingly.
    void recover(uint8_t i);
};

}  // namespace sensors
}  // namespace smartrc
