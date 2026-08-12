#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "Config.h"
#include "Overlay.h"

// Full-screen status cards, generated a row at a time so they stream to the
// panel exactly like a photo does and never need a framebuffer in RAM.
//
// These are deliberately NOT server-rendered. Everything else in this project
// pushes image work to the backend, but a card that says "the battery is
// empty" is the one image that has to be guaranteed present without a
// network, without a sync having ever succeeded, and without anyone having
// remembered to run `pio run -t uploadfs`. It costs ~200 bytes of font and no
// flash. Pure, so the geometry is testable — see test/test_native.

namespace polaroid::card {

// Classic 5x7 cell font, one byte per column, bit 0 = top row. Index 0 is
// space, 1..26 are A..Z. Uppercase only, which is all the fixed strings need.
inline constexpr std::array<std::array<std::uint8_t, 5>, 27> FONT{{
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
}};

constexpr std::uint8_t glyphIndex(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<std::uint8_t>(c - 'A' + 1);
    }
    if (c >= 'a' && c <= 'z') {
        return static_cast<std::uint8_t>(c - 'a' + 1);
    }
    return 0;
}

inline constexpr std::uint16_t GLYPH_W = 5;
inline constexpr std::uint16_t GLYPH_H = 7;
inline constexpr std::uint16_t GLYPH_ADVANCE = 6;

constexpr std::uint16_t textWidth(std::string_view text, std::uint16_t scale) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        ((text.size() - 1) * GLYPH_ADVANCE + GLYPH_W) * scale);
}

constexpr std::uint16_t textLeftFor(std::string_view text, std::uint16_t scale) {
    const std::uint16_t width = textWidth(text, scale);
    return width >= config::PANEL_WIDTH
               ? 0
               : static_cast<std::uint16_t>((config::PANEL_WIDTH - width) / 2);
}

// Draws one row of a text run. `y` is the panel row; nothing happens unless it
// falls inside the glyph band.
constexpr void textRow(std::span<std::uint8_t> row, std::uint16_t y, std::string_view text,
                       std::uint16_t left, std::uint16_t top, std::uint16_t scale,
                       config::Ink ink) {
    if (y < top || y >= top + GLYPH_H * scale) {
        return;
    }
    const std::uint16_t glyphRow = static_cast<std::uint16_t>((y - top) / scale);

    for (std::size_t i = 0; i < text.size(); i++) {
        const auto& glyph = FONT[glyphIndex(text[i])];
        const std::uint16_t originX = static_cast<std::uint16_t>(left + i * GLYPH_ADVANCE * scale);

        for (std::uint16_t column = 0; column < GLYPH_W; column++) {
            if ((glyph[column] >> glyphRow) & 1u) {
                for (std::uint16_t s = 0; s < scale; s++) {
                    setPixel(row, static_cast<std::uint16_t>(originX + column * scale + s), ink);
                }
            }
        }
    }
}

constexpr void horizontalSpan(std::span<std::uint8_t> row, std::uint16_t x0, std::uint16_t x1,
                              config::Ink ink) {
    for (std::uint16_t x = x0; x < x1; x++) {
        setPixel(row, x, ink);
    }
}

// ---------------------------------------------------------------- empty card

// Laid out to match the Polaroid frame the photos use (see frame.ts): the
// battery glyph sits inside the square image area, the words sit on the chin —
// so it reads as a Polaroid somebody wrote on, not as an error screen.
namespace empty_card {

inline constexpr std::uint16_t BODY_X0 = 90;
inline constexpr std::uint16_t BODY_X1 = 300;
inline constexpr std::uint16_t BODY_Y0 = 130;
inline constexpr std::uint16_t BODY_Y1 = 270;
inline constexpr std::uint16_t STROKE = 8;

// The terminal nub, so the shape reads as a battery and not as a rectangle.
inline constexpr std::uint16_t NUB_X1 = 318;
inline constexpr std::uint16_t NUB_Y0 = 175;
inline constexpr std::uint16_t NUB_Y1 = 225;

inline constexpr std::string_view LINE = "CHARGE ME";
inline constexpr std::uint16_t TEXT_SCALE = 6;
inline constexpr std::uint16_t TEXT_TOP = 470;

static_assert(BODY_X1 <= config::PANEL_WIDTH);
static_assert(NUB_X1 <= config::PANEL_WIDTH);
static_assert(TEXT_TOP + GLYPH_H * TEXT_SCALE <= config::PANEL_HEIGHT);

}  // namespace empty_card

// The last thing the device ever draws. E-ink holds it with no power at all,
// so this sits on the fridge indefinitely telling the couple what's wrong —
// which is the entire point of drawing it while there's still charge to
// finish a refresh, rather than letting the panel freeze mid-photo.
inline void emptyBatteryCardRow(std::uint16_t y, std::span<std::uint8_t> row) {
    using namespace empty_card;
    using config::INK_RED;
    using config::INK_WHITE;

    // Paper first. Nothing is composited under this — it is the whole image.
    for (auto& byte : row) {
        byte = static_cast<std::uint8_t>((INK_WHITE << 4) | INK_WHITE);
    }

    const bool inBody = y >= BODY_Y0 && y < BODY_Y1;
    if (inBody) {
        const bool isCap = y < BODY_Y0 + STROKE || y >= BODY_Y1 - STROKE;
        if (isCap) {
            horizontalSpan(row, BODY_X0, BODY_X1, INK_RED);
        } else {
            horizontalSpan(row, BODY_X0, BODY_X0 + STROKE, INK_RED);
            horizontalSpan(row, BODY_X1 - STROKE, BODY_X1, INK_RED);
        }
    }

    if (y >= NUB_Y0 && y < NUB_Y1) {
        horizontalSpan(row, BODY_X1, NUB_X1, INK_RED);
    }

    textRow(row, y, LINE, textLeftFor(LINE, TEXT_SCALE), TEXT_TOP, TEXT_SCALE,
            config::INK_BLACK);
}

}  // namespace polaroid::card
