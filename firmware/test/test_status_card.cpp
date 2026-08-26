#include <unity.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "StatusCard.h"

using namespace polaroid;
using namespace config;

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
    return getPixel(
        std::span<const std::uint8_t>(frame.data() + y * PANEL_ROW_BYTES, PANEL_ROW_BYTES), x);
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

// The battery used to be a hollow outline, on the reasoning that an empty
// battery should read as empty. The artwork is now Material's
// battery_charging_full, which is a solid body with the charging bolt knocked
// out of it — so the thing that carries the meaning is the bolt, not the
// emptiness, and "is the middle white" is no longer the right question. What
// still has to hold is that the bolt is a HOLE: if the icon's two subpaths
// ever stop being XOR-ed, the bolt fills in and the glyph turns into a slab.
void test_empty_card_bolt_is_knocked_out_of_the_battery() {
    const auto frame = renderEmptyCard();
    using namespace card::art;

    const std::uint16_t midY = ICON_Y + ICON_H / 2;
    const std::uint16_t midX = ICON_X + ICON_W / 2;

    // Body present at both shoulders of the icon's vertical midpoint.
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, ICON_X + 6, midY));
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, ICON_X + ICON_W - 7, midY));

    // The bolt crosses the centre line, so paper shows through there.
    TEST_ASSERT_EQUAL(INK_WHITE, pixelAt(frame, midX, midY));

    // ...and the hole is bounded. Far enough above and below the waist of the
    // bolt the centre column is solid again, which a filled-in or a runaway
    // cutout would both fail.
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, midX, ICON_Y + 20));
    TEST_ASSERT_EQUAL(INK_RED, pixelAt(frame, midX, ICON_Y + ICON_H - 20));
}

void test_empty_card_draws_readable_text() {
    const auto frame = renderEmptyCard();
    const std::size_t black = countInk(frame, INK_BLACK);

    // "CHARGE ME" set in Jost 700 at 46px is a few thousand pixels. Zero means
    // the bitmap lookup walked off the end of the array and drew nothing.
    TEST_ASSERT_TRUE(black > 1000);
}

void test_card_text_is_centred_and_on_panel() {
    using namespace card::art;

    TEST_ASSERT_TRUE(TEXT_X + TEXT_W <= PANEL_WIDTH);
    // Equal margins either side, within a pixel of rounding.
    TEST_ASSERT_TRUE(PANEL_WIDTH - (TEXT_X + TEXT_W) - TEXT_X <= 1);
}

// The glyph belongs in the square image area and the words on the chin, so the
// card matches the Polaroid frame the photos use.
void test_card_respects_the_polaroid_frame() {
    using namespace card::art;
    constexpr std::uint16_t IMAGE_BOTTOM = 400;  // border + square image, see frame.ts
    TEST_ASSERT_TRUE(ICON_Y + ICON_H < IMAGE_BOTTOM);
    TEST_ASSERT_TRUE(TEXT_Y > IMAGE_BOTTOM);
}

void runStatusCardTests() {
    // Unity reports whichever file main() is in otherwise.
    Unity.TestFile = __FILE__;
    RUN_TEST(test_empty_card_fills_every_pixel_with_a_legal_ink);
    RUN_TEST(test_empty_card_is_mostly_paper);
    RUN_TEST(test_empty_card_bolt_is_knocked_out_of_the_battery);
    RUN_TEST(test_empty_card_draws_readable_text);
    RUN_TEST(test_card_text_is_centred_and_on_panel);
    RUN_TEST(test_card_respects_the_polaroid_frame);
}
