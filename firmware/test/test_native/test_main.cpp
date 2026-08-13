#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "BatteryCurve.h"
#include "Manifest.h"
#include "Overlay.h"
#include "StatusCard.h"

using namespace polaroid;
using namespace config;

namespace {

using Row = std::array<std::uint8_t, PANEL_ROW_BYTES>;

}  // namespace

// ------------------------------------------------------------ manifest diff

void test_diff_empty_local_fetches_everything() {
    Manifest local;
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "h1", 100));
    remote.photos.push_back(makePhoto("b", "h2", 200));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(2, diff.fetch.size());
    TEST_ASSERT_EQUAL(0, diff.remove.size());
    TEST_ASSERT_EQUAL(0, diff.unchanged);
}

void test_diff_identical_fetches_nothing() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "h1", 100));
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "h1", 100));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_TRUE(diff.empty());
    TEST_ASSERT_EQUAL(1, diff.unchanged);
}

// The hash is over the packed framebuffer, so re-tuning the dither has to
// invalidate every photo even though nothing about the source images changed.
void test_diff_changed_hash_refetches() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "old", 100));
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "new", 100));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(1, diff.fetch.size());
    TEST_ASSERT_EQUAL(0, diff.remove.size());
}

void test_diff_removed_remotely_is_deleted_locally() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "h1", 100));
    local.photos.push_back(makePhoto("b", "h2", 200));
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "h1", 100));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(0, diff.fetch.size());
    TEST_ASSERT_EQUAL(1, diff.remove.size());
    TEST_ASSERT_EQUAL_STRING("b", diff.remove[0].id.data());
}

// A sync that returns nothing must not wipe the device. This is the failure
// mode that would leave the couple staring at a blank frame.
void test_diff_empty_remote_removes_all_but_fetches_none() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "h1", 100));
    Manifest remote;

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(0, diff.fetch.size());
    TEST_ASSERT_EQUAL(1, diff.remove.size());
}

// ------------------------------------------------------------ cycling

void test_next_index_wraps() {
    TEST_ASSERT_EQUAL(1, nextIndex(0, 3));
    TEST_ASSERT_EQUAL(2, nextIndex(1, 3));
    TEST_ASSERT_EQUAL(0, nextIndex(2, 3));
}

void test_next_index_survives_empty_device() {
    TEST_ASSERT_EQUAL(0, nextIndex(0, 0));
    TEST_ASSERT_EQUAL(0, nextIndex(7, 0));
}

void test_newest_index_picks_latest_upload() {
    Manifest manifest;
    manifest.photos.push_back(makePhoto("a", "h", 300));
    manifest.photos.push_back(makePhoto("b", "h", 100));
    manifest.photos.push_back(makePhoto("c", "h", 900));

    TEST_ASSERT_EQUAL(2, newestIndex(manifest));
}

void test_newest_index_on_empty_manifest() {
    Manifest manifest;
    TEST_ASSERT_EQUAL(0, newestIndex(manifest));
}

// ------------------------------------------------------------ battery curve

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

// ------------------------------------------------------------ 4bpp packing

void test_pixel_round_trip_both_nibbles() {
    Row row{};

    setPixel(row, 0, INK_RED);
    setPixel(row, 1, INK_BLUE);

    TEST_ASSERT_EQUAL(INK_RED, getPixel(row, 0));
    TEST_ASSERT_EQUAL(INK_BLUE, getPixel(row, 1));
    TEST_ASSERT_EQUAL_HEX8(0x35, row[0]);
}

void test_pixel_write_does_not_disturb_neighbour() {
    Row row{};
    row.fill(0x66);

    setPixel(row, 4, INK_BLACK);

    TEST_ASSERT_EQUAL(INK_BLACK, getPixel(row, 4));
    TEST_ASSERT_EQUAL(INK_GREEN, getPixel(row, 5));
    TEST_ASSERT_EQUAL(INK_GREEN, getPixel(row, 3));
}

void test_pixel_write_past_edge_is_ignored() {
    Row row{};
    row.fill(0x11);

    setPixel(row, PANEL_WIDTH, INK_RED);
    setPixel(row, PANEL_WIDTH + 500, INK_RED);

    for (std::size_t i = 0; i < row.size(); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x11, row[i]);
    }
}

// ------------------------------------------------------------ overlay

void test_overlay_skips_rows_outside_the_icon() {
    Row row{};
    row.fill(0x66);

    TEST_ASSERT_FALSE(lowBatteryOverlay(0, row));
    TEST_ASSERT_FALSE(lowBatteryOverlay(icon::Y - 1, row));
    TEST_ASSERT_FALSE(lowBatteryOverlay(icon::Y + icon::HEIGHT, row));

    for (std::size_t i = 0; i < row.size(); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x66, row[i]);
    }
}

void test_overlay_draws_border_on_its_first_row() {
    Row row{};
    row.fill(0x66);

    TEST_ASSERT_TRUE(lowBatteryOverlay(icon::Y, row));

    // Top edge of the battery outline is solid ink across its width.
    for (uint16_t i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL(INK_RED, getPixel(row, icon::X + i));
    }
    // Untouched outside the icon box.
    TEST_ASSERT_EQUAL(INK_GREEN, getPixel(row, icon::X - 2));
}

// The photo underneath is dithered noise. Without clearing to white first the
// battery's interior fills with whatever colour the sky happened to be.
void test_overlay_clears_paper_under_the_icon_interior() {
    Row row{};
    row.fill(0x66);

    TEST_ASSERT_TRUE(lowBatteryOverlay(icon::Y + 5, row));

    TEST_ASSERT_EQUAL(INK_RED, getPixel(row, icon::X));
    TEST_ASSERT_EQUAL(INK_WHITE, getPixel(row, icon::X + 5));
    TEST_ASSERT_EQUAL(INK_WHITE, getPixel(row, icon::X + 10));
}

void test_overlay_stays_inside_the_panel() {
    TEST_ASSERT_TRUE(icon::X + icon::WIDTH <= PANEL_WIDTH);
    TEST_ASSERT_TRUE(icon::Y + icon::HEIGHT <= PANEL_HEIGHT);
}

// ------------------------------------------------------------ status card

namespace {

// Rebuilds the whole card into a full framebuffer so tests can ask questions
// about the finished image rather than about one row at a time.
std::vector<std::uint8_t> renderEmptyCard() {
    std::vector<std::uint8_t> frame(PANEL_BYTES);
    for (std::uint16_t y = 0; y < PANEL_HEIGHT; y++) {
        card::emptyBatteryCardRow(
            y, std::span<std::uint8_t>(frame.data() + y * PANEL_ROW_BYTES, PANEL_ROW_BYTES));
    }
    return frame;
}

std::uint8_t pixelAt(const std::vector<std::uint8_t>& frame, std::uint16_t x, std::uint16_t y) {
    return getPixel(std::span<const std::uint8_t>(frame.data() + y * PANEL_ROW_BYTES,
                                                  PANEL_ROW_BYTES),
                    x);
}

std::size_t countInk(const std::vector<std::uint8_t>& frame, config::Ink ink) {
    std::size_t total = 0;
    for (std::uint16_t y = 0; y < PANEL_HEIGHT; y++) {
        for (std::uint16_t x = 0; x < PANEL_WIDTH; x++) {
            if (pixelAt(frame, x, y) == ink) total++;
        }
    }
    return total;
}

}  // namespace

// The card is generated, not composited, so it has to fill every byte itself.
// A row function that only drew its shapes would leave whatever was in the
// static row buffer from the previous row smeared down the panel.
void test_empty_card_fills_every_pixel_with_a_legal_ink() {
    const auto frame = renderEmptyCard();

    for (std::uint16_t y = 0; y < PANEL_HEIGHT; y++) {
        for (std::uint16_t x = 0; x < PANEL_WIDTH; x++) {
            const std::uint8_t ink = pixelAt(frame, x, y);
            const bool legal = ink == INK_BLACK || ink == INK_WHITE || ink == INK_YELLOW ||
                               ink == INK_RED || ink == INK_BLUE || ink == INK_GREEN;
            TEST_ASSERT_TRUE(legal);
        }
    }
}

void test_empty_card_is_mostly_paper() {
    const auto frame = renderEmptyCard();
    const std::size_t white = countInk(frame, INK_WHITE);
    TEST_ASSERT_TRUE(white > (PANEL_WIDTH * PANEL_HEIGHT) / 2);
}

void test_empty_card_draws_a_hollow_battery() {
    const auto frame = renderEmptyCard();
    using namespace card::empty_card;

    // Outline present on all four sides at the body's vertical midpoint.
    const std::uint16_t midY = (BODY_Y0 + BODY_Y1) / 2;
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, BODY_X0 + 1, midY));
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, BODY_X1 - 2, midY));
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, (BODY_X0 + BODY_X1) / 2, BODY_Y0 + 1));
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, (BODY_X0 + BODY_X1) / 2, BODY_Y1 - 2));

    // Hollow: an empty battery reads as empty. A filled one would say the
    // opposite of what the card is for.
    TEST_ASSERT_EQUAL(INK_WHITE, pixelAt(frame, (BODY_X0 + BODY_X1) / 2, midY));

    // The nub is what makes the shape a battery rather than a rectangle.
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, BODY_X1 + 2, (NUB_Y0 + NUB_Y1) / 2));
}

void test_empty_card_draws_readable_text() {
    const auto frame = renderEmptyCard();
    const std::size_t black = countInk(frame, INK_BLACK);

    // "CHARGE ME" at scale 6 is a few thousand pixels. Zero means the font
    // lookup silently returned spaces.
    TEST_ASSERT_TRUE(black > 1000);
}

void test_card_text_is_centred_and_on_panel() {
    using card::textLeftFor;
    using card::textWidth;
    using namespace card::empty_card;

    const std::uint16_t left = textLeftFor(LINE, TEXT_SCALE);
    const std::uint16_t width = textWidth(LINE, TEXT_SCALE);

    TEST_ASSERT_TRUE(left + width <= PANEL_WIDTH);
    // Equal margins either side, within a pixel of rounding.
    TEST_ASSERT_TRUE(PANEL_WIDTH - (left + width) - left <= 1);
}

// The battery glyph belongs in the square image area and the words on the
// chin, so the card matches the Polaroid frame the photos use.
void test_card_respects_the_polaroid_frame() {
    using namespace card::empty_card;
    constexpr std::uint16_t IMAGE_BOTTOM = 400;  // border + square image, see frame.ts
    TEST_ASSERT_TRUE(BODY_Y1 < IMAGE_BOTTOM);
    TEST_ASSERT_TRUE(TEXT_TOP > IMAGE_BOTTOM);
}

void test_font_maps_letters_and_falls_back_to_space() {
    TEST_ASSERT_EQUAL(1, card::glyphIndex('A'));
    TEST_ASSERT_EQUAL(1, card::glyphIndex('a'));
    TEST_ASSERT_EQUAL(26, card::glyphIndex('Z'));
    TEST_ASSERT_EQUAL(0, card::glyphIndex(' '));
    TEST_ASSERT_EQUAL(0, card::glyphIndex('!'));
    TEST_ASSERT_EQUAL(0, card::glyphIndex('7'));
}

// ------------------------------------------------------------ battery policy

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

// ------------------------------------------------------------ capacity

// A download stages a full framebuffer as /p/.partial before renaming over the
// old file. If MAX_PHOTOS filled the filesystem exactly, that staging write
// would fail and the device could never replace a photo once full.
void test_photo_limit_leaves_room_to_stage_a_download() {
    constexpr std::uint32_t LITTLEFS_BYTES = 0x620000;  // partitions_8mb.csv
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

// ------------------------------------------------------------ pinout

// The XIAO breaks out exactly eleven GPIO and this design needs exactly
// eleven, so a duplicate is not a warning anywhere — it is two peripherals
// silently fighting over a line on a board with no headers left to probe.
// Runs against whichever branch of Config.h is compiled in.
void test_no_pin_is_used_twice() {
    const std::array pins{PIN_EPD_SCK,  PIN_EPD_MOSI,   PIN_EPD_CS,     PIN_EPD_DC,
                          PIN_EPD_RST,  PIN_EPD_BUSY,   PIN_EPD_PWR,    PIN_I2C_SCL,
                          PIN_I2C_SDA,  PIN_ACCEL_INT1, PIN_VBAT_SENSE};

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

// ------------------------------------------------------------ geometry

void test_framebuffer_size_matches_the_panel() {
    TEST_ASSERT_EQUAL(400, PANEL_WIDTH);
    TEST_ASSERT_EQUAL(600, PANEL_HEIGHT);
    TEST_ASSERT_EQUAL(200, PANEL_ROW_BYTES);
    TEST_ASSERT_EQUAL(120000, PANEL_BYTES);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_diff_empty_local_fetches_everything);
    RUN_TEST(test_diff_identical_fetches_nothing);
    RUN_TEST(test_diff_changed_hash_refetches);
    RUN_TEST(test_diff_removed_remotely_is_deleted_locally);
    RUN_TEST(test_diff_empty_remote_removes_all_but_fetches_none);

    RUN_TEST(test_next_index_wraps);
    RUN_TEST(test_next_index_survives_empty_device);
    RUN_TEST(test_newest_index_picks_latest_upload);
    RUN_TEST(test_newest_index_on_empty_manifest);

    RUN_TEST(test_battery_endpoints);
    RUN_TEST(test_battery_is_monotonic);
    RUN_TEST(test_battery_knots_are_not_linear);
    RUN_TEST(test_battery_low_threshold_fires_before_empty);

    RUN_TEST(test_pixel_round_trip_both_nibbles);
    RUN_TEST(test_pixel_write_does_not_disturb_neighbour);
    RUN_TEST(test_pixel_write_past_edge_is_ignored);

    RUN_TEST(test_overlay_skips_rows_outside_the_icon);
    RUN_TEST(test_overlay_draws_border_on_its_first_row);
    RUN_TEST(test_overlay_clears_paper_under_the_icon_interior);
    RUN_TEST(test_overlay_stays_inside_the_panel);

    RUN_TEST(test_empty_card_fills_every_pixel_with_a_legal_ink);
    RUN_TEST(test_empty_card_is_mostly_paper);
    RUN_TEST(test_empty_card_draws_a_hollow_battery);
    RUN_TEST(test_empty_card_draws_readable_text);
    RUN_TEST(test_card_text_is_centred_and_on_panel);
    RUN_TEST(test_card_respects_the_polaroid_frame);
    RUN_TEST(test_font_maps_letters_and_falls_back_to_space);

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

    return UNITY_END();
}
