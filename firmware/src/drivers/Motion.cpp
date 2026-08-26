#include "Motion.h"

#include <Adafruit_LIS3DH.h>
#include <Wire.h>

#include <cstdint>

using namespace config;

namespace polaroid {

namespace {

Adafruit_LIS3DH accel;

constexpr uint8_t REG_CTRL1 = 0x20;
constexpr uint8_t REG_CTRL2 = 0x21;
constexpr uint8_t REG_CTRL3 = 0x22;
constexpr uint8_t REG_CTRL5 = 0x24;
constexpr uint8_t REG_INT1_CFG = 0x30;
constexpr uint8_t REG_INT1_SRC = 0x31;
constexpr uint8_t REG_INT1_THS = 0x32;
constexpr uint8_t REG_INT1_DUR = 0x33;
constexpr uint8_t REG_CTRL4 = 0x23;
constexpr uint8_t REG_REFERENCE = 0x26;

// CTRL4's FS1:FS0 field, in bits 5:4.
constexpr uint8_t rangeBits() {
    switch (ACCEL_RANGE_G) {
        case 2: return 0x00;
        case 8: return 0x20;
        case 16: return 0x30;
        default: return 0x10;  // 4 g
    }
}

void writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ACCEL_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t readRegister(uint8_t reg) {
    Wire.beginTransmission(ACCEL_I2C_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(static_cast<uint8_t>(ACCEL_I2C_ADDRESS), static_cast<uint8_t>(1));
    return Wire.available() ? Wire.read() : 0;
}

}  // namespace

bool Motion::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    pinMode(PIN_ACCEL_INT1, INPUT);

    present_ = accel.begin(ACCEL_I2C_ADDRESS);
    if (!present_) {
        return false;
    }

    /*
     * Written directly, not via setRange(): the library's begin() sets HR in
     * CTRL4, setRange() only touches FS, and CTRL1 below sets LPen. HR and
     * LPen together is not a mode the part defines.
     */
    writeRegister(REG_CTRL4, 0x80 | rangeBits());  // BDU, HR clear

    // POWER: 50 Hz low-power mode, ~2 uA. setDataRate has no low-power variant.
    writeRegister(REG_CTRL1, 0x4F);  // ODR 50 Hz, LPen, XYZ enabled

    /*
     * HP_IA1 filters gravity out of the activity generator. Without it INT1 is
     * pinned high forever: 1 g against a 1.25 g threshold at whatever angle
     * the frame hangs.
     */
    writeRegister(REG_CTRL2, 0x0D);  // FDS, HPCLICK, HP_IA1

    /*
     * Resets the filter. Skipping it leaves the power-on offset and the
     * detector goes deaf; so does reading it before the part has settled after
     * the ODR change above. 100 ms is one turn-on plus four filter periods.
     */
    delay(100);
    readRegister(REG_REFERENCE);

    configureActivityDetector();

    writeRegister(REG_CTRL3, 0x40);  // I1_IA1: activity routes to INT1
    writeRegister(REG_CTRL5, 0x08);  // LIR_INT1: latched

    return true;
}

void Motion::configureActivityDetector() {
    writeRegister(REG_INT1_THS, ACTIVITY_THRESHOLD & 0x7F);
    writeRegister(REG_INT1_DUR, ACTIVITY_DURATION);
    writeRegister(REG_INT1_CFG, 0x2A);  // OR of high events on X, Y, Z
}

void Motion::clearWakeLatch() {
    if (!present_) {
        return;
    }
    readRegister(REG_INT1_SRC);
}

void Motion::armForSleep() {
    if (!present_) {
        return;
    }

    /*
     * Clearing the latch once is not enough: while the frame is still moving
     * the detector re-asserts within about a millisecond.
     */
    const std::uint32_t deadline = millis() + MOTION_SETTLE_TIMEOUT_MS;
    std::uint32_t lowSince = 0;

    while (millis() < deadline) {
        readRegister(REG_INT1_SRC);

        if (digitalRead(PIN_ACCEL_INT1) == LOW) {
            if (lowSince == 0) {
                lowSince = millis();
            } else if (millis() - lowSince >= MOTION_SETTLE_MS) {
                return;
            }
        } else {
            lowSince = 0;
        }

        delay(5);
    }
}

void Motion::powerDown() {
    Wire.end();
}

}  // namespace polaroid
