#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "Config.h"

/*
 * Nibble-level compositing, applied to one row at a time while the framebuffer
 * streams to the panel. Pure — see test/test_native.
 */

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

namespace offline_icon {

/*
 * Three signal bars with a slash, bottom-left so it sits beside the battery
 * icon without either moving.
 */
inline constexpr std::uint16_t WIDTH = 24;
inline constexpr std::uint16_t HEIGHT = 12;
inline constexpr std::uint16_t X = 12;
inline constexpr std::uint16_t Y = config::PANEL_HEIGHT - HEIGHT - 12;

inline constexpr std::uint16_t BAR_W = 5;
inline constexpr std::uint16_t BAR_PITCH = 8;
// Shortest bar first, so the group reads as signal strength.
inline constexpr std::array<std::uint16_t, 3> BAR_HEIGHTS{4, 8, 12};

static_assert(X + WIDTH <= config::PANEL_WIDTH);
static_assert(Y + HEIGHT <= config::PANEL_HEIGHT);

}  // namespace offline_icon

/*
 * Composites the offline icon into a row if the row intersects it. Returns
 * whether it touched anything, so the caller can skip the work on the other
 * 588 rows. Costs nothing beyond the refresh we were already doing.
 */
constexpr bool offlineOverlay(std::uint16_t row, std::span<std::uint8_t> rowBytes) {
    using namespace offline_icon;
    if (row < Y || row >= Y + HEIGHT) {
        return false;
    }

    const std::uint16_t local = static_cast<std::uint16_t>(row - Y);

    for (std::uint16_t x = 0; x < WIDTH; x++) {
        setPixel(rowBytes, static_cast<std::uint16_t>(X + x), config::INK_WHITE);
    }

    for (std::uint16_t bar = 0; bar < BAR_HEIGHTS.size(); bar++) {
        if (local < HEIGHT - BAR_HEIGHTS[bar]) {
            continue;
        }
        for (std::uint16_t x = 0; x < BAR_W; x++) {
            setPixel(rowBytes, static_cast<std::uint16_t>(X + bar * BAR_PITCH + x),
                     config::INK_RED);
        }
    }

    // Slash from bottom-left to top-right, three pixels wide.
    const std::uint16_t slashX = static_cast<std::uint16_t>((HEIGHT - 1 - local) * 2);
    for (std::uint16_t x = 0; x < 3; x++) {
        setPixel(rowBytes, static_cast<std::uint16_t>(X + slashX + x), config::INK_BLACK);
    }
    return true;
}

}  // namespace polaroid
