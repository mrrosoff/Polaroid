#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "CardArt.h"
#include "Config.h"
#include "Overlay.h"

/*
 * Full-screen status cards, generated a row at a time so they stream to the
 * panel like a photo and never need a framebuffer in RAM.
 *
 * Not server-rendered, unlike every other image here: "the battery is empty"
 * has to be drawable with no network and no sync having ever succeeded.
 *
 * Artwork is two 1bpp bitmaps in CardArt.h, from tools/gencard.py.
 */

namespace polaroid::card {

// ---------------------------------------------------------------- empty card

/*
 * Glyph above, words below, both on white paper. Positions and sizes live in
 * CardArt.h next to the bits they describe, so the layout cannot drift from
 * the artwork it was measured against.
 */
namespace empty_card {

/*
 * The card mirrors the Polaroid frame the photos use: glyph inside the square
 * image area, words on the chin below it.
 */
inline constexpr std::uint16_t IMAGE_BOTTOM = 400;

static_assert(art::ICON_X + art::ICON_W <= config::PANEL_WIDTH);
static_assert(art::ICON_Y + art::ICON_H < IMAGE_BOTTOM,
              "the glyph has grown out of the frame's image area");
static_assert(art::TEXT_Y > IMAGE_BOTTOM, "the words have climbed off the chin and into the image");
static_assert(art::TEXT_X + art::TEXT_W <= config::PANEL_WIDTH);
static_assert(art::TEXT_Y + art::TEXT_H <= config::PANEL_HEIGHT);

}  // namespace empty_card

/*
 * One row of a packed 1bpp bitmap, blitted in a single ink. `y` is the panel
 * row; nothing happens unless it falls inside the bitmap's band.
 */
constexpr void bitmapRow(std::span<std::uint8_t> row, std::uint16_t y,
                         std::span<const std::uint8_t> bits, std::uint16_t stride,
                         std::uint16_t width, std::uint16_t height, std::uint16_t left,
                         std::uint16_t top, config::Ink ink) {
    if (y < top || y >= top + height) {
        return;
    }
    const std::size_t base = static_cast<std::size_t>(y - top) * stride;
    for (std::uint16_t x = 0; x < width; x++) {
        if ((bits[base + (x >> 3)] >> (7 - (x & 7))) & 1u) {
            setPixel(row, static_cast<std::uint16_t>(left + x), ink);
        }
    }
}

/*
 * The last thing the device ever draws. E-ink holds it with no power at all,
 * so this sits on the fridge indefinitely telling the couple what's wrong —
 * which is the entire point of drawing it while there's still charge to
 * finish a refresh, rather than letting the panel freeze mid-photo.
 */
inline void emptyBatteryCardRow(std::uint16_t y, std::span<std::uint8_t> row) {
    using config::INK_BLACK;
    using config::INK_RED;
    using config::INK_WHITE;

    // Paper first. Nothing is composited under this — it is the whole image.
    for (auto& byte : row) {
        byte = static_cast<std::uint8_t>((INK_WHITE << 4) | INK_WHITE);
    }

    /*
     * The bolt is a hole in the icon, not a shape drawn over it, so the paper
     * laid down above is what shows through it.
     */
    bitmapRow(row, y, art::ICON_BITS, art::ICON_STRIDE, art::ICON_W, art::ICON_H, art::ICON_X,
              art::ICON_Y, INK_RED);
    bitmapRow(row, y, art::TEXT_BITS, art::TEXT_STRIDE, art::TEXT_W, art::TEXT_H, art::TEXT_X,
              art::TEXT_Y, INK_BLACK);
}

}  // namespace polaroid::card
