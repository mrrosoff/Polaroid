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
constexpr uint8_t REG_REFERENCE = 0x26;

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

    switch (ACCEL_RANGE_G) {
        case 2: accel.setRange(LIS3DH_RANGE_2_G); break;
        case 8: accel.setRange(LIS3DH_RANGE_8_G); break;
        case 16: accel.setRange(LIS3DH_RANGE_16_G); break;
        default: accel.setRange(LIS3DH_RANGE_4_G); break;
    }

    // POWER: 50 Hz low-power mode, ~2 µA. Fast enough to resolve the reversals
    // in a shake, slow enough to disappear from the budget. Written directly
    // because the Adafruit library's setDataRate has no low-power variant.
    writeRegister(REG_CTRL1, 0x4F);  // ODR 50 Hz, LPen, XYZ enabled

    // High-pass filter the interrupt paths so gravity doesn't hold the activity
    // threshold permanently tripped whatever angle the frame hangs at.
    //
    // CTRL2 is HPM1 HPM0 HPCF2 HPCF1 FDS HPCLICK HP_IA2 HP_IA1. Bit 0 is the
    // one that filters the activity generator, and leaving it clear pins INT1
    // high forever: gravity is 1 g and the threshold is 0.75 g, so the device
    // reads every wake as a fridge open and never gets back to sleep.
    writeRegister(REG_CTRL2, 0x0D);

    // With HPM=00 the filter is reset by reading REFERENCE. Skip it and the
    // filter keeps whatever offset it powered up with, which can hold the
    // activity detector either permanently tripped or permanently deaf.
    readRegister(REG_REFERENCE);

    configureActivityDetector();

    // Activity routes to INT1, latched.
    writeRegister(REG_CTRL3, 0x40);  // I1_IA1
    writeRegister(REG_CTRL5, 0x08);  // LIR_INT1

    return true;
}

void Motion::configureActivityDetector() {
    writeRegister(REG_INT1_THS, ACTIVITY_THRESHOLD & 0x7F);
    writeRegister(REG_INT1_DUR, ACTIVITY_DURATION);
    writeRegister(REG_INT1_CFG, 0x2A);  // OR of high events on X, Y, Z
}

MotionEvent Motion::classifyWakeEvent() {
    if (!present_) {
        return MotionEvent::None;
    }

    // Reading this is also what releases the latched INT1.
    const uint8_t activitySource = readRegister(REG_INT1_SRC);
    return (activitySource & 0x40) != 0 ? MotionEvent::Shake : MotionEvent::None;
}

void Motion::armForSleep() {
    if (!present_) {
        return;
    }

    // Clearing the latch is not enough on its own: ext0 is level-triggered, and
    // if the frame is still moving the detector re-asserts within about a
    // millisecond, so we would wake again the instant we slept. Keep clearing
    // until the line has stayed low long enough to mean the movement is over.
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

void Motion::powerDown() { Wire.end(); }

}  // namespace polaroid
