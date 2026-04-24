#pragma once

#include <ArduinoJson.h>

#include "sensors/Mpu6050.h"

// Sensor aggregator. Owns every concrete sensor driver and exposes a
// single pair of calls (begin/update) plus the JSON-append surface that
// Portal and WsServer use to publish readings.
//
// Add a new sensor in 3 steps:
//   1. Drop Foo.{h,cpp} into this folder.
//   2. Wire up begin()/update() below.
//   3. Add a branch to appendSensorsJson() that emits its payload under
//      a stable key. The React app's Monitor page already renders
//      sensors.imu / sensors.distance / sensors.battery automatically.
namespace smartrc {
namespace sensors {

void begin();
void update();

/** Apply NVS-persisted axis-invert flags. Safe to call anytime. */
void setImuInverts(bool x, bool y, bool z);

/**
 * Re-run gyro bias calibration. Blocks for ~2 s — the vehicle MUST be
 * stationary the whole time. Used on-demand from the CLI / API when the
 * gyro has drifted (e.g., after temperature changes).
 */
void recalibrateImu();

/**
 * Append every available sensor's payload under `parent["sensors"]`.
 * Used by both Portal::handleStatus() and WsServer's telemetry builder
 * — same wire shape on REST and WS.
 */
void appendSensorsJson(JsonObject parent);

// Direct access for the Serial CLI's `imu` diagnostic command.
const Mpu6050& imu();

}  // namespace sensors
}  // namespace smartrc
