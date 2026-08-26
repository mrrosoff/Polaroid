#include <unity.h>

#include <cstdint>

#include "BatteryCurve.h"

using namespace polaroid;
using namespace config;

void test_battery_endpoints() {
    TEST_ASSERT_EQUAL(100, voltageToPercent(4.20f));
    TEST_ASSERT_EQUAL(100, voltageToPercent(4.15f));
    TEST_ASSERT_EQUAL(0, voltageToPercent(3.30f));
    TEST_ASSERT_EQUAL(0, voltageToPercent(3.00f));
}

void test_battery_is_monotonic() {
    std::uint8_t previous = 0;
    for (float volts = 3.30f; volts <= 4.20f; volts += 0.01f) {
        const std::uint8_t percent = voltageToPercent(volts);
        TEST_ASSERT_TRUE(percent >= previous);
        previous = percent;
    }
}

// The whole reason the curve is piecewise: a linear map would call 3.75 V
// "50%" when it is really about 40% of the runtime remaining, and would still
// be claiming 22% at 3.5 V when there are days left, not weeks.
void test_battery_knots_are_not_linear() {
    TEST_ASSERT_EQUAL(40, voltageToPercent(3.75f));
    TEST_ASSERT_EQUAL(15, voltageToPercent(3.50f));
}

void test_battery_low_threshold_fires_before_empty() {
    TEST_ASSERT_TRUE(voltageToPercent(3.50f) <= LOW_BATTERY_PERCENT);
    TEST_ASSERT_TRUE(voltageToPercent(3.75f) > LOW_BATTERY_PERCENT);
}

void runBatteryCurveTests() {
    // Unity reports whichever file main() is in otherwise.
    Unity.TestFile = __FILE__;
    RUN_TEST(test_battery_endpoints);
    RUN_TEST(test_battery_is_monotonic);
    RUN_TEST(test_battery_knots_are_not_linear);
    RUN_TEST(test_battery_low_threshold_fires_before_empty);
}
