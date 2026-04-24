#include "sensors/Mpu6050.h"

#include <Arduino.h>
#include <Wire.h>

namespace smartrc {
namespace sensors {

namespace {
// Register map (datasheet RM-MPU-6000A-00 §3).
constexpr uint8_t REG_SMPLRT_DIV   = 0x19;
constexpr uint8_t REG_CONFIG       = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG  = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;  // burst-read 14 bytes from here
constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;
constexpr uint8_t REG_WHO_AM_I     = 0x75;

// Scale factors — default full-scale ranges (§6.2.8 / §6.4.3).
constexpr float ACCEL_LSB_PER_G     = 16384.0f;   // ±2g
constexpr float G_TO_MS2            = 9.80665f;
constexpr float GYRO_LSB_PER_DPS    = 131.0f;     // ±250°/s

constexpr uint16_t READ_INTERVAL_MS = 20;         // 50 Hz polling
}  // namespace

bool Mpu6050::begin(uint8_t i2cAddress) {
    address_ = i2cAddress;

    // Wake the chip — it powers on in sleep (PWR_MGMT_1.SLEEP = 1). Writing
    // 0 also selects the internal 8 MHz oscillator, which is fine for us.
    if (!writeReg(REG_PWR_MGMT_1, 0x00)) {
        Serial.printf("[mpu6050] not found at 0x%02X\n", address_);
        present_ = false;
        return false;
    }
    delay(10);

    // WHO_AM_I is nominally 0x68 but many clones return different values
    // (some even return 0x98). We log it, don't gate on it — the wake
    // above is the real liveness test.
    uint8_t who = 0;
    if (readRegs(REG_WHO_AM_I, &who, 1)) {
        Serial.printf("[mpu6050] WHO_AM_I=0x%02X\n", who);
    }

    // Modest low-pass + 50 Hz effective output rate.
    //   CONFIG.DLPF_CFG = 3  → 44 Hz accel / 42 Hz gyro LPF
    //   SMPLRT_DIV      = 19 → gyro 1 kHz / (1+19) = 50 Hz
    writeReg(REG_CONFIG,       0x03);
    writeReg(REG_SMPLRT_DIV,   19);
    writeReg(REG_GYRO_CONFIG,  0x00);  // FS_SEL = 0 → ±250°/s
    writeReg(REG_ACCEL_CONFIG, 0x00);  // AFS_SEL = 0 → ±2g

    present_ = true;
    Serial.printf("[mpu6050] initialised @ 0x%02X (fs: ±2g, ±250°/s, 50 Hz)\n",
                  address_);

    // Auto-calibrate gyro bias — only accurate if the vehicle is still
    // during boot. Takes ~2.4 s. Run on demand later via
    // Mpu6050::calibrateGyroBias() if you ever move the car mid-boot.
    calibrateGyroBias();
    return true;
}

void Mpu6050::calibrateGyroBias(uint16_t samples) {
    if (!present_) return;
    Serial.printf("[mpu6050] calibrating gyro bias — hold still ~%.1fs...\n",
                  samples * 0.02f);

    double sx = 0, sy = 0, sz = 0;
    uint16_t taken = 0;
    const uint32_t deadline = millis() + (uint32_t)samples * 40;

    while (taken < samples && (int32_t)(millis() - deadline) < 0) {
        uint8_t b[14];
        if (!readRegs(REG_ACCEL_XOUT_H, b, sizeof(b))) { delay(5); continue; }
        const int16_t gx = (int16_t)(((uint16_t)b[8]  << 8) | b[9]);
        const int16_t gy = (int16_t)(((uint16_t)b[10] << 8) | b[11]);
        const int16_t gz = (int16_t)(((uint16_t)b[12] << 8) | b[13]);
        sx += gx; sy += gy; sz += gz;
        ++taken;
        delay(20);
    }
    if (taken == 0) {
        Serial.println("[mpu6050] calibration failed (no samples)");
        return;
    }
    gxBiasRaw_ = (float)(sx / taken);
    gyBiasRaw_ = (float)(sy / taken);
    gzBiasRaw_ = (float)(sz / taken);
    biasCalibrated_ = true;
    Serial.printf("[mpu6050] gyro bias (°/s): x=%+.2f y=%+.2f z=%+.2f (%u samples)\n",
                  gxBiasRaw_ / GYRO_LSB_PER_DPS,
                  gyBiasRaw_ / GYRO_LSB_PER_DPS,
                  gzBiasRaw_ / GYRO_LSB_PER_DPS,
                  taken);
}

float Mpu6050::gyroBiasDps(uint8_t axis) const {
    switch (axis) {
        case 0: return gxBiasRaw_ / GYRO_LSB_PER_DPS;
        case 1: return gyBiasRaw_ / GYRO_LSB_PER_DPS;
        case 2: return gzBiasRaw_ / GYRO_LSB_PER_DPS;
    }
    return 0;
}

bool Mpu6050::update() {
    if (!present_) return false;

    const uint32_t now = millis();
    if (now - lastReadMs_ < READ_INTERVAL_MS) return false;
    lastReadMs_ = now;

    // Burst-read accel (6) + temp (2) + gyro (6) = 14 bytes, big-endian.
    uint8_t b[14];
    if (!readRegs(REG_ACCEL_XOUT_H, b, sizeof(b))) {
        last_.valid = false;
        return false;
    }
    auto be16 = [](uint8_t hi, uint8_t lo) -> int16_t {
        return (int16_t)(((uint16_t)hi << 8) | lo);
    };
    const int16_t axR = be16(b[0],  b[1]);
    const int16_t ayR = be16(b[2],  b[3]);
    const int16_t azR = be16(b[4],  b[5]);
    const int16_t tR  = be16(b[6],  b[7]);
    const int16_t gxR = be16(b[8],  b[9]);
    const int16_t gyR = be16(b[10], b[11]);
    const int16_t gzR = be16(b[12], b[13]);

    // Physical units in vehicle frame. Gyro has a per-chip bias offset
    // that varies with temperature; we subtract the stationary-sampled
    // bias captured in calibrateGyroBias() so "at rest" actually reads
    // zero instead of a 3-5 °/s factory offset.
    float ax = ((float)axR / ACCEL_LSB_PER_G) * G_TO_MS2;
    float ay = ((float)ayR / ACCEL_LSB_PER_G) * G_TO_MS2;
    float az = ((float)azR / ACCEL_LSB_PER_G) * G_TO_MS2;
    float gx = ((float)gxR - gxBiasRaw_) / GYRO_LSB_PER_DPS;
    float gy = ((float)gyR - gyBiasRaw_) / GYRO_LSB_PER_DPS;
    float gz = ((float)gzR - gzBiasRaw_) / GYRO_LSB_PER_DPS;

    // Apply mount-orientation signs (same flip on accel + gyro per axis).
    if (invertX_) { ax = -ax; gx = -gx; }
    if (invertY_) { ay = -ay; gy = -gy; }
    if (invertZ_) { az = -az; gz = -gz; }

    last_.ax = ax; last_.ay = ay; last_.az = az;
    last_.gx = gx; last_.gy = gy; last_.gz = gz;
    // Datasheet §4.18 temp = raw/340 + 36.53 °C.
    last_.temperature_c = (float)tR / 340.0f + 36.53f;
    last_.valid = true;
    last_.ts_ms = now;
    return true;
}

bool Mpu6050::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool Mpu6050::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    const size_t got = Wire.requestFrom((int)address_, (int)len);
    if (got != len) return false;
    for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)Wire.read();
    return true;
}

}  // namespace sensors
}  // namespace smartrc
