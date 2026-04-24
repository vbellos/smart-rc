#include "sensors/Sensors.h"

#include <math.h>

#include "Pins.h"
#include "sensors/Mpu6050.h"

namespace smartrc {
namespace sensors {

namespace {
Mpu6050 g_mpu;

// Round to 2 decimals to keep JSON compact on the wire (saves ~4 bytes
// per field @ 50 Hz × 7 fields ≈ 1.4 kB/s).
inline float r2(float v) {
    if (!isfinite(v)) return 0.0f;
    return roundf(v * 100.0f) / 100.0f;
}
}  // namespace

void begin() {
    g_mpu.begin(pins::MPU6050_ADDR);
}

void update() {
    g_mpu.update();
}

void setImuInverts(bool x, bool y, bool z) {
    g_mpu.setInverts(x, y, z);
}

void recalibrateImu() {
    g_mpu.calibrateGyroBias();
}

void appendSensorsJson(JsonObject parent) {
    JsonObject s = parent["sensors"].to<JsonObject>();

    if (g_mpu.present()) {
        const auto& m = g_mpu.last();
        JsonObject imu = s["imu"].to<JsonObject>();
        imu["gx"] = r2(m.gx);
        imu["gy"] = r2(m.gy);
        imu["gz"] = r2(m.gz);
        imu["ax"] = r2(m.ax);
        imu["ay"] = r2(m.ay);
        imu["az"] = r2(m.az);
        imu["temp_c"]     = r2(m.temperature_c);
        imu["valid"]      = m.valid;
        imu["calibrated"] = g_mpu.gyroCalibrated();
    }
    // Future sensors land here:
    //   if (g_tof.present()) { ... s["distance"] ... }
    //   if (g_ina.present()) { ... s["battery"]  ... }
}

const Mpu6050& imu() { return g_mpu; }

}  // namespace sensors
}  // namespace smartrc
