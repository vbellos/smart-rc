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
     * Sample the IMU while stationary and capture both gyro AND accel
     * biases. Blocks for roughly samples * 20 ms. Call from begin() on
     * boot, or on demand via the CLI's `imu calibrate` command. Accel
     * bias covers X/Y only — Z is left alone so gravity still reads
     * ~+9.8 m/s² as a sanity check.
     */
    void calibrateGyroBias(uint16_t samples = 120);

    /** True once calibrateGyroBias() has run successfully. */
    bool gyroCalibrated() const { return biasCalibrated_; }

    /** Bias in °/s for each gyro axis (raw, no invert applied). */
    float gyroBiasDps(uint8_t axis) const;

    /**
     * Dead-reckoned forward-axis velocity in m/s. Integrated from
     * bias-corrected `ax`. Direction-correct over short horizons; drifts
     * on long idles, but ZUPT (zero-velocity update) zeroes it out after
     * ~1.5 s of quiescence so "hand-pushed → coast → brake" works too.
     */
    float velocityX() const { return vx_; }

    /** True when ZUPT thinks the vehicle is stationary. */
    bool  isStationary() const { return stationary_; }

    /**
     * Force vx to zero right now. Called by Drive::finishBrake() because
     * we've just actively brought the vehicle to rest and don't want to
     * wait for the ZUPT window to do the same thing — plus motor
     * vibration during the brake usually corrupts the integrator so its
     * residual is untrustworthy anyway.
     */
    void resetVelocity();

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

    // Biases in raw LSB units (pre-scaling). Subtracted in update().
    float    gxBiasRaw_ = 0;
    float    gyBiasRaw_ = 0;
    float    gzBiasRaw_ = 0;
    float    axBiasRaw_ = 0;
    float    ayBiasRaw_ = 0;
    bool     biasCalibrated_ = false;

    // Velocity integrator (m/s, vehicle frame). ZUPT zeroes it after
    // kAxQuietSamples of low-ax samples so short drives don't drift
    // forever.
    float    vx_            = 0;
    uint16_t axQuietCount_  = 0;
    bool     stationary_    = true;
    uint32_t lastSampleMs_  = 0;
};

}  // namespace sensors
}  // namespace smartrc
