#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "Config.h"

// Nibble-level compositing, applied to one row at a time while the framebuffer
// streams to the panel. Pure — see test/test_native.

namespace polaroid {

// 4bpp, two pixels per byte, high nibble first.
constexpr void setPixel(std::span<std::uint8_t> rowBytes, std::uint16_t x, config::Ink ink) {
    if (x >= config::PANEL_WIDTH) {
        return;
    }
    std::uint8_t& byte = rowBytes[x / 2];
    if ((x & 1) == 0) {
        byte = static_cast<std::uint8_t>((byte & 0x0F) | (ink << 4));
    } else {
        byte = static_cast<std::uint8_t>((byte & 0xF0) | (ink & 0x0F));
    }
}

[[nodiscard]] constexpr std::uint8_t getPixel(std::span<const std::uint8_t> rowBytes,
                                              std::uint16_t x) {
    const std::uint8_t byte = rowBytes[x / 2];
    return (x & 1) == 0 ? static_cast<std::uint8_t>(byte >> 4)
                        : static_cast<std::uint8_t>(byte & 0x0F);
}

namespace icon {

// A 24 x 12 battery, bottom-right, inside the Polaroid chin where there is
// already white paper — so it reads as an indicator and not as damage.
inline constexpr std::uint16_t WIDTH = 24;
inline constexpr std::uint16_t HEIGHT = 12;
inline constexpr std::uint16_t X = config::PANEL_WIDTH - WIDTH - 12;
inline constexpr std::uint16_t Y = config::PANEL_HEIGHT - HEIGHT - 12;

// 1 = ink, 0 = leave the paper alone. Outline plus a nub on the right.
inline constexpr std::array<std::uint32_t, HEIGHT> BITMAP{
    0b111111111111111111110000, 0b100000000000000000010000, 0b100000000000000000010000,
    0b100000000000000000011000, 0b100000000000000000011000, 0b100000000000000000011000,
    0b100000000000000000011000, 0b100000000000000000011000, 0b100000000000000000011000,
    0b100000000000000000010000, 0b100000000000000000010000, 0b111111111111111111110000,
};

static_assert(X + WIDTH <= config::PANEL_WIDTH);
static_assert(Y + HEIGHT <= config::PANEL_HEIGHT);

}  // namespace icon

// Composites the low-battery icon into a row if the row intersects it. Returns
// whether it touched anything, so the caller can skip the work on the other
// 588 rows. Costs nothing beyond the refresh we were already doing.
constexpr bool lowBatteryOverlay(std::uint16_t row, std::span<std::uint8_t> rowBytes) {
    if (row < icon::Y || row >= icon::Y + icon::HEIGHT) {
        return false;
    }

    const std::uint32_t bits = icon::BITMAP[row - icon::Y];
    for (std::uint16_t i = 0; i < icon::WIDTH; i++) {
        const bool set = (bits >> (icon::WIDTH - 1 - i)) & 1u;
        // Clear the paper under the whole icon box, then draw. Otherwise the
        // dithered noise of the photo shows through the battery's interior.
        setPixel(rowBytes, icon::X + i, set ? config::INK_RED : config::INK_WHITE);
    }
    return true;
}

}  // namespace polaroid
