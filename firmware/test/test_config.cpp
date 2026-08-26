#include <unity.h>

#include <array>
#include <cstdint>

#include "Config.h"

using namespace config;

// The card costs one 30 s refresh. Redrawing it on every wake would spend the
// last of the battery repainting an image that is already on the panel, so
// recovery must sit well clear of the critical threshold.
void test_battery_thresholds_have_hysteresis() {
    TEST_ASSERT_TRUE(BATTERY_RECOVERY_PERCENT > CRITICAL_BATTERY_PERCENT);
    TEST_ASSERT_TRUE(BATTERY_RECOVERY_PERCENT - CRITICAL_BATTERY_PERCENT >= 10);
}

void test_low_warning_comes_before_critical() {
    TEST_ASSERT_TRUE(LOW_BATTERY_PERCENT > CRITICAL_BATTERY_PERCENT);
}

// There has to be real charge left at the critical threshold, or the final
// refresh browns out and freezes a half-drawn frame on the panel forever.
void test_critical_threshold_leaves_charge_for_one_last_refresh() {
    constexpr float PACK_MAH = 2000.0f;
    constexpr float REFRESH_MAH = 0.4f;
    const float remaining = PACK_MAH * (CRITICAL_BATTERY_PERCENT / 100.0f);
    TEST_ASSERT_TRUE(remaining > REFRESH_MAH * 20);
}

void test_empty_check_interval_is_slower_than_normal_refresh() {
    TEST_ASSERT_TRUE(EMPTY_CHECK_INTERVAL_SECONDS > REFRESH_INTERVAL_SECONDS);
}

// A download stages a full framebuffer as /p/.partial before renaming over the
// old file. If MAX_PHOTOS filled the filesystem exactly, that staging write
// would fail and the device could never replace a photo once full.
void test_photo_limit_leaves_room_to_stage_a_download() {
    constexpr std::uint32_t LITTLEFS_BYTES = 0x620000;  // partitions.csv
    constexpr std::uint32_t used = static_cast<std::uint32_t>(MAX_PHOTOS) * PANEL_BYTES;

    TEST_ASSERT_TRUE(used < LITTLEFS_BYTES);
    TEST_ASSERT_TRUE(LITTLEFS_BYTES - used > PANEL_BYTES);
}

// The manifest is capped server-side, but the device parses into a fixed
// budget. If the firmware's limit were the smaller of the two it would
// silently drop photos the server considers live.
void test_photo_limit_matches_the_service() {
    TEST_ASSERT_EQUAL(50, MAX_PHOTOS);
}

// The XIAO breaks out exactly eleven GPIO and this design needs exactly
// eleven, so a duplicate is not a warning anywhere — it is two peripherals
// silently fighting over a line on a board with no headers left to probe.
// Runs against whichever branch of Config.h is compiled in.
void test_no_pin_is_used_twice() {
    const std::array pins{PIN_EPD_SCK, PIN_EPD_MOSI,   PIN_EPD_CS,    PIN_EPD_DC,
                          PIN_EPD_RST, PIN_EPD_BUSY,   PIN_EPD_PWR,   PIN_I2C_SCL,
                          PIN_I2C_SDA, PIN_ACCEL_INT1, PIN_VBAT_SENSE};

    for (std::size_t i = 0; i < pins.size(); i++) {
        for (std::size_t j = i + 1; j < pins.size(); j++) {
            TEST_ASSERT_NOT_EQUAL(pins[i], pins[j]);
        }
    }
}

// GPIO0-21 is the RTC domain on the S3. An INT1 outside it cannot wake the
// chip from deep sleep at all, and the failure looks like "shake doesn't work"
// rather than like a build error.
void test_wake_pin_is_in_the_rtc_domain() {
    TEST_ASSERT_TRUE(PIN_ACCEL_INT1 >= 0 && PIN_ACCEL_INT1 <= 21);
}

// ADC2 stops answering while WiFi is up, which would make the battery read
// garbage during exactly the sync where we want to report it.
void test_battery_sense_is_on_adc1() {
    TEST_ASSERT_TRUE(PIN_VBAT_SENSE >= 1 && PIN_VBAT_SENSE <= 10);
}

void test_unused_pins_are_iterable_even_when_empty() {
    int count = 0;
    for (int pin : UNUSED_PINS) {
        TEST_ASSERT_TRUE(pin >= 0);
        count++;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(UNUSED_PINS.size()), count);
}

void test_framebuffer_size_matches_the_panel() {
    TEST_ASSERT_EQUAL(400, PANEL_WIDTH);
    TEST_ASSERT_EQUAL(600, PANEL_HEIGHT);
    TEST_ASSERT_EQUAL(200, PANEL_ROW_BYTES);
    TEST_ASSERT_EQUAL(120000, PANEL_BYTES);
}

void runConfigTests() {
    // Unity reports whichever file main() is in otherwise.
    Unity.TestFile = __FILE__;
    RUN_TEST(test_battery_thresholds_have_hysteresis);
    RUN_TEST(test_low_warning_comes_before_critical);
    RUN_TEST(test_critical_threshold_leaves_charge_for_one_last_refresh);
    RUN_TEST(test_empty_check_interval_is_slower_than_normal_refresh);
    RUN_TEST(test_photo_limit_leaves_room_to_stage_a_download);
    RUN_TEST(test_photo_limit_matches_the_service);
    RUN_TEST(test_no_pin_is_used_twice);
    RUN_TEST(test_wake_pin_is_in_the_rtc_domain);
    RUN_TEST(test_battery_sense_is_on_adc1);
    RUN_TEST(test_unused_pins_are_iterable_even_when_empty);
    RUN_TEST(test_framebuffer_size_matches_the_panel);
}
