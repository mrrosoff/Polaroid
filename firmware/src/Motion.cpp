#include "Motion.h"

#include <Adafruit_LIS3DH.h>
#include <Wire.h>

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
constexpr uint8_t REG_CLICK_CFG = 0x38;
constexpr uint8_t REG_CLICK_SRC = 0x39;
constexpr uint8_t REG_CLICK_THS = 0x3A;
constexpr uint8_t REG_TIME_LIMIT = 0x3B;
constexpr uint8_t REG_TIME_LATENCY = 0x3C;
constexpr uint8_t REG_TIME_WINDOW = 0x3D;

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
    writeRegister(REG_CTRL2, 0x0C);

    configureClickDetector();
    configureActivityDetector();

    // Both detectors route to INT1, latched.
    writeRegister(REG_CTRL3, 0xC0);  // I1_CLICK | I1_IA1
    writeRegister(REG_CTRL5, 0x08);  // LIR_INT1

    return true;
}

void Motion::configureClickDetector() {
    writeRegister(REG_CLICK_THS, CLICK_THRESHOLD & 0x7F);
    writeRegister(REG_TIME_LIMIT, CLICK_TIME_LIMIT);
    writeRegister(REG_TIME_LATENCY, CLICK_TIME_LATENCY);
    writeRegister(REG_TIME_WINDOW, CLICK_TIME_WINDOW);

    // Double-click on all three axes. Single-click fires when someone sets a
    // glass down on the shelf next to it; requiring two reversals inside the
    // window is what makes "shake" actually mean shake.
    writeRegister(REG_CLICK_CFG, CLICK_REQUIRE_DOUBLE ? 0x2A : 0x15);
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

    // Order matters. Read CLICK_SRC first: a shake also trips the lower
    // activity threshold, so checking activity first would classify every shake
    // as a fridge open.
    uint8_t clickSource = readRegister(REG_CLICK_SRC);
    uint8_t activitySource = readRegister(REG_INT1_SRC);

    bool clicked = (clickSource & 0x40) != 0;  // IA
    if (clicked) {
        bool doubleClick = (clickSource & 0x20) != 0;
        if (!CLICK_REQUIRE_DOUBLE || doubleClick) {
            return MotionEvent::Shake;
        }
    }

    if ((activitySource & 0x40) != 0) {
        return MotionEvent::Fridge;
    }

    return MotionEvent::None;
}

void Motion::armForSleep() {
    if (!present_) {
        return;
    }
    // Clear both latches so INT1 is released. If it is still asserted when we
    // call esp_deep_sleep_start, ext0 fires immediately and we spin.
    readRegister(REG_CLICK_SRC);
    readRegister(REG_INT1_SRC);
}

void Motion::powerDown() { Wire.end(); }

}  // namespace polaroid
