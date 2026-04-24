#pragma once

#include <stdint.h>
#include <stddef.h>

namespace smartrc {
namespace sensors {

// Minimal MPU6050 driver.
// - No external libs — just <Wire.h>. Full rolled ~120 LOC.
// - Fixed full-scale: accel ±2g, gyro ±250°/s (good default for an RC).
// - Outputs in vehicle-frame physical units (m/s² and °/s).
// - Axis-inversion flags let the user dial out mount orientation without
//   re-soldering.
class Mpu6050 {
public:
    struct Sample {
        float ax = 0, ay = 0, az = 0;        // m/s²
        float gx = 0, gy = 0, gz = 0;        // °/s
        float temperature_c = 0;             // °C (die sensor)
        bool  valid         = false;
        uint32_t ts_ms      = 0;             // millis() at sample time
    };

    /** Probe + init. Returns true if the chip ACKs and configuration sticks. */
    bool begin(uint8_t i2cAddress = 0x68);

    /** Call every loop(). Reads at fixed cadence (default 50 Hz).
     *  Returns true if a new sample was captured this call. */
    bool update();

    const Sample& last() const { return last_; }
    bool present() const { return present_; }

    /** Axis-invert flags — flip sign on both accel & gyro for that axis. */
    void setInverts(bool x, bool y, bool z) {
        invertX_ = x; invertY_ = y; invertZ_ = z;
    }
    bool invertX() const { return invertX_; }
    bool invertY() const { return invertY_; }
    bool invertZ() const { return invertZ_; }

    /**
     * Average `samples` gyro readings while the vehicle is stationary and
     * subtract the bias from all future outputs. Blocks for roughly
     * samples * 20 ms. Call from begin() on boot, or on demand via the
     * CLI's `imu calibrate` command.
     */
    void calibrateGyroBias(uint16_t samples = 120);

    /** True once calibrateGyroBias() has run successfully. */
    bool gyroCalibrated() const { return biasCalibrated_; }

    /** Bias in °/s for each gyro axis (raw, no invert applied). */
    float gyroBiasDps(uint8_t axis) const;

private:
    bool writeReg(uint8_t reg, uint8_t val);
    bool readRegs(uint8_t reg, uint8_t* buf, size_t len);

    uint8_t  address_    = 0x68;
    bool     present_    = false;
    bool     invertX_    = false;
    bool     invertY_    = false;
    bool     invertZ_    = false;
    uint32_t lastReadMs_ = 0;
    Sample   last_       = {};

    // Gyro bias in raw LSB units (pre-scaling). Subtracted in update().
    float    gxBiasRaw_ = 0;
    float    gyBiasRaw_ = 0;
    float    gzBiasRaw_ = 0;
    bool     biasCalibrated_ = false;
};

}  // namespace sensors
}  // namespace smartrc
