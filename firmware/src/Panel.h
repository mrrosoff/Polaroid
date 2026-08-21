#pragma once

#include <Arduino.h>
#include <FS.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include "Span.h"
#include <string_view>

#include "Config.h"

namespace polaroid {

using PanelRow = std::array<std::uint8_t, config::PANEL_ROW_BYTES>;

// Per-row patch applied while streaming, used for the low-battery icon.
// Returning false leaves the row untouched.
using RowOverlay = bool (*)(std::uint16_t row, std::span<std::uint8_t> rowBytes);

// Fills a row from nothing, for status cards that have no source file.
using RowGenerator = void (*)(std::uint16_t row, std::span<std::uint8_t> rowBytes);

// RAII: constructing brings the rail up and runs the init sequence,
// destructing puts the panel to sleep and cuts the gate. Panel power is the
// one resource in this firmware that must never leak, so it is tied to a
// scope rather than to remembering to call a function.
class Panel {
  public:
    Panel() = default;
    ~Panel();

    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    [[nodiscard]] bool begin();
    void powerDown();

    [[nodiscard]] bool ready() const noexcept { return initialized_; }

    bool displayFile(fs::FS& filesystem, std::string_view path, RowOverlay overlay = nullptr);
    bool displayGenerated(RowGenerator generate);
    bool displaySolid(config::Ink ink);

  private:
    void reset();
    void sendCommand(std::uint8_t command);
    void sendData(std::uint8_t data);
    void sendData(std::initializer_list<std::uint8_t> data);
    void sendDataBulk(std::span<const std::uint8_t> data);
    [[nodiscard]] bool waitUntilIdle();
    bool refresh();

    bool initialized_ = false;
};

}  // namespace polaroid
