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

// ---------------------------------------------------------------------- cards

/*
 * Glyph above, words below, both on white paper. Positions and sizes live in
 * CardArt.h next to the bits they describe, so the layout cannot drift from
 * the artwork it was measured against.
 *
 * The cards mirror the Polaroid frame the photos use: glyph inside the square
 * image area, words on the chin below it.
 */
namespace layout {

inline constexpr std::uint16_t IMAGE_BOTTOM = 400;

}  // namespace layout

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
 * A card is two bitmaps and where they sit. Bundling them means the row
 * function is written once and each card is data, not another copy of the
 * blitting loop.
 */
struct CardArt {
    config::Ink iconInk;
    std::span<const std::uint8_t> iconBits;
    std::uint16_t iconStride, iconW, iconH, iconX, iconY;
    std::span<const std::uint8_t> textBits;
    std::uint16_t textStride, textW, textH, textX, textY;
};

/* Designated, because twelve positional fields is a renumbering waiting to go
 * wrong the next time one is added. */
inline constexpr CardArt BATTERY_CARD{
    .iconInk = config::INK_RED,
    .iconBits = art::ICON_BITS,
    .iconStride = art::ICON_STRIDE,
    .iconW = art::ICON_W,
    .iconH = art::ICON_H,
    .iconX = art::ICON_X,
    .iconY = art::ICON_Y,
    .textBits = art::TEXT_BITS,
    .textStride = art::TEXT_STRIDE,
    .textW = art::TEXT_W,
    .textH = art::TEXT_H,
    .textX = art::TEXT_X,
    .textY = art::TEXT_Y,
};

/* Blue, not red: an empty frame is waiting, not broken. */
inline constexpr CardArt NO_PHOTOS_CARD{
    .iconInk = config::INK_BLUE,
    .iconBits = art::NO_PHOTOS_ICON_BITS,
    .iconStride = art::NO_PHOTOS_ICON_STRIDE,
    .iconW = art::NO_PHOTOS_ICON_W,
    .iconH = art::NO_PHOTOS_ICON_H,
    .iconX = art::NO_PHOTOS_ICON_X,
    .iconY = art::NO_PHOTOS_ICON_Y,
    .textBits = art::NO_PHOTOS_TEXT_BITS,
    .textStride = art::NO_PHOTOS_TEXT_STRIDE,
    .textW = art::NO_PHOTOS_TEXT_W,
    .textH = art::NO_PHOTOS_TEXT_H,
    .textX = art::NO_PHOTOS_TEXT_X,
    .textY = art::NO_PHOTOS_TEXT_Y,
};

constexpr bool fitsTheFrame(const CardArt& c) {
    return c.iconX + c.iconW <= config::PANEL_WIDTH && c.iconY + c.iconH < layout::IMAGE_BOTTOM &&
           c.textY > layout::IMAGE_BOTTOM && c.textX + c.textW <= config::PANEL_WIDTH &&
           c.textY + c.textH <= config::PANEL_HEIGHT;
}

static_assert(fitsTheFrame(BATTERY_CARD), "the battery card has grown out of the Polaroid frame");
static_assert(fitsTheFrame(NO_PHOTOS_CARD), "the empty card has grown out of the Polaroid frame");

/*
 * Red glyph, black words, white paper, nothing composited underneath -- the
 * card IS the whole image. Cut-outs in the glyph are holes, so the paper laid
 * down first is what shows through them.
 */
constexpr void cardRow(std::uint16_t y, std::span<std::uint8_t> row, const CardArt& c) {
    for (auto& byte : row) {
        byte = static_cast<std::uint8_t>((config::INK_WHITE << 4) | config::INK_WHITE);
    }
    bitmapRow(row, y, c.iconBits, c.iconStride, c.iconW, c.iconH, c.iconX, c.iconY, c.iconInk);
    bitmapRow(row, y, c.textBits, c.textStride, c.textW, c.textH, c.textX, c.textY,
              config::INK_BLACK);
}

/*
 * The last thing the device ever draws. E-ink holds it with no power at all,
 * so this sits on the fridge indefinitely telling the couple what's wrong --
 * which is the entire point of drawing it while there's still charge to finish
 * a refresh, rather than letting the panel freeze mid-photo.
 */
inline void emptyBatteryCardRow(std::uint16_t y, std::span<std::uint8_t> row) {
    cardRow(y, row, BATTERY_CARD);
}

/*
 * Shown when the library is empty: every photo deleted, or a device that has
 * never synced one. Without it the panel keeps whatever it last drew, so a
 * frame with nothing in it looks identical to a frame that stopped working.
 */
inline void noPhotosCardRow(std::uint16_t y, std::span<std::uint8_t> row) {
    cardRow(y, row, NO_PHOTOS_CARD);
}

}  // namespace polaroid::card
