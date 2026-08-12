#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "BatteryCurve.h"
#include "Manifest.h"
#include "Overlay.h"

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

    RUN_TEST(test_framebuffer_size_matches_the_panel);

    return UNITY_END();
}
