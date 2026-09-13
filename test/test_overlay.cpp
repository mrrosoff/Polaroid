#include <unity.h>

#include <array>
#include <cstdint>
#include <span>

#include "Overlay.h"

using namespace polaroid;
using namespace config;

namespace {

using Row = std::array<std::uint8_t, PANEL_ROW_BYTES>;

}  // namespace

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

void test_offline_overlay_skips_rows_outside_the_icon() {
    Row row{};
    row.fill(0x66);

    TEST_ASSERT_FALSE(offlineOverlay(0, row));
    TEST_ASSERT_FALSE(offlineOverlay(offline_icon::Y - 1, row));
    TEST_ASSERT_FALSE(offlineOverlay(offline_icon::Y + offline_icon::HEIGHT, row));

    for (std::size_t i = 0; i < row.size(); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x66, row[i]);
    }
}

void test_offline_overlay_draws_the_tallest_bar_on_every_row() {
    Row row{};
    row.fill(0x66);

    // The third bar is full height, so it is inked on the icon's top row.
    TEST_ASSERT_TRUE(offlineOverlay(offline_icon::Y, row));
    const std::uint16_t tallBar = offline_icon::X + 2 * offline_icon::BAR_PITCH;
    TEST_ASSERT_EQUAL(INK_RED, getPixel(row, tallBar));

    // The shortest bar is not, and the paper under it is cleared rather than
    // left showing the photo's dither.
    TEST_ASSERT_EQUAL(INK_WHITE, getPixel(row, offline_icon::X + 1));
}

void test_offline_overlay_draws_a_slash() {
    Row row{};
    row.fill(0x66);
    offlineOverlay(offline_icon::Y + offline_icon::HEIGHT - 1, row);

    // Bottom row: the slash sits at the icon's left edge.
    TEST_ASSERT_EQUAL(INK_BLACK, getPixel(row, offline_icon::X));
}

void runOverlayTests() {
    // Unity reports whichever file main() is in otherwise.
    Unity.TestFile = __FILE__;
    RUN_TEST(test_pixel_round_trip_both_nibbles);
    RUN_TEST(test_pixel_write_does_not_disturb_neighbour);
    RUN_TEST(test_pixel_write_past_edge_is_ignored);
    RUN_TEST(test_offline_overlay_skips_rows_outside_the_icon);
    RUN_TEST(test_offline_overlay_draws_the_tallest_bar_on_every_row);
    RUN_TEST(test_offline_overlay_draws_a_slash);
}
