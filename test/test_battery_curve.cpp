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

/*
 * The knots are the curve. Asserting them directly is the only way a change to
 * one gets noticed -- the shape between them is interpolation, but these four
 * points are the fit to a real LiPo and are not arbitrary.
 */
void test_battery_curve_hits_its_knots() {
    TEST_ASSERT_EQUAL(100, voltageToPercent(VBAT_FULL_V));
    TEST_ASSERT_EQUAL(40, voltageToPercent(VBAT_NOMINAL_V));
    TEST_ASSERT_EQUAL(15, voltageToPercent(VBAT_LOW_V));
    TEST_ASSERT_EQUAL(0, voltageToPercent(VBAT_EMPTY_V));
}

void runBatteryCurveTests() {
    // Unity reports whichever file main() is in otherwise.
    Unity.TestFile = __FILE__;
    RUN_TEST(test_battery_endpoints);
    RUN_TEST(test_battery_is_monotonic);
    RUN_TEST(test_battery_knots_are_not_linear);
    RUN_TEST(test_battery_curve_hits_its_knots);
}
