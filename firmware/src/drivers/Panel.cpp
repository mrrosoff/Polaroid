#include "Panel.h"

#include <SPI.h>

#include <algorithm>

using namespace config;

namespace polaroid {

namespace {

SPIClass panelSpi(FSPI);

void writePin(int pin, bool level) {
    digitalWrite(pin, level ? HIGH : LOW);
}

/*
 * The panel's power-on sequence, transcribed from Waveshare's EPD_4in0e
 * driver. It is vendor data, not logic: every value is a magic number from
 * their reference and none of it is ours to reason about, so it reads better
 * as a table than as forty calls.
 */
struct InitCommand {
    std::uint8_t command;
    std::span<const std::uint8_t> data;
};

constexpr std::uint8_t CMDH[] = {0x49, 0x55, 0x20, 0x08, 0x09, 0x18};
constexpr std::uint8_t PWR[] = {0x3F};
constexpr std::uint8_t PSR[] = {0x5F, 0x69};
constexpr std::uint8_t POFS[] = {0x40, 0x1F, 0x1F, 0x2C};
constexpr std::uint8_t BTST2[] = {0x6F, 0x1F, 0x1F, 0x22};
constexpr std::uint8_t BTST3[] = {0x6F, 0x1F, 0x17, 0x17};
constexpr std::uint8_t BTST1[] = {0x00, 0x54, 0x00, 0x44};
constexpr std::uint8_t TSE[] = {0x02, 0x00};
constexpr std::uint8_t PLL[] = {0x08};
constexpr std::uint8_t CDI[] = {0x3F};

// Resolution: 0x0190 x 0x0258 = 400 x 600.
static_assert(PANEL_WIDTH == 0x0190 && PANEL_HEIGHT == 0x0258, "TRES below must match Config.h");
constexpr std::uint8_t TRES[] = {0x01, 0x90, 0x02, 0x58};

constexpr std::uint8_t PWS[] = {0x2F};
constexpr std::uint8_t AN_TM[] = {0x01};

constexpr InitCommand INIT_SEQUENCE[] = {
    {0xAA, CMDH},  {0x01, PWR},   {0x00, PSR},   {0x05, POFS}, {0x08, BTST2},
    {0x06, BTST3}, {0x03, BTST1}, {0x60, TSE},   {0x30, PLL},  {0x50, CDI},
    {0x61, TRES},  {0xE3, PWS},   {0x84, AN_TM},
};

}  // namespace

Panel::~Panel() {
    powerDown();
}

bool Panel::begin() {
    pinMode(PIN_EPD_PWR, OUTPUT);
    writePin(PIN_EPD_PWR, true);
    delay(10);

    for (int pin : {PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST}) {
        pinMode(pin, OUTPUT);
    }
    pinMode(PIN_EPD_BUSY, INPUT);
    writePin(PIN_EPD_CS, true);

    panelSpi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
    panelSpi.beginTransaction(SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));

    reset();
    if (!waitUntilIdle()) {
        return false;
    }
    delay(30);

    /*
     * Per-byte, not sendDataBulk: the bulk path holds CS low across the whole
     * burst, and the init sequence is framed one byte at a time like the
     * vendor driver does it.
     */
    for (const InitCommand& step : INIT_SEQUENCE) {
        sendCommand(step.command);
        for (std::uint8_t byte : step.data) {
            sendData(byte);
        }
    }

    initialized_ = waitUntilIdle();
    return initialized_;
}

void Panel::powerDown() {
    if (initialized_) {
        sendCommand(0x07);  // DEEP_SLEEP
        sendData(0xA5);
        delay(10);

        panelSpi.endTransaction();
        panelSpi.end();
    }

    /*
     * POWER: the panel's own deep-sleep command still leaves the driver
     * board's regulator idling in the hundreds of uA. Cutting the gate is what
     * actually removes it from the budget.
     */
    writePin(PIN_EPD_PWR, false);

    for (int pin : {PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_SCK, PIN_EPD_MOSI, PIN_EPD_BUSY}) {
        pinMode(pin, INPUT_PULLDOWN);
    }

    initialized_ = false;
}

void Panel::reset() {
    writePin(PIN_EPD_RST, true);
    delay(20);
    writePin(PIN_EPD_RST, false);
    delay(2);
    writePin(PIN_EPD_RST, true);
    delay(20);
}

void Panel::sendCommand(std::uint8_t command) {
    writePin(PIN_EPD_DC, false);
    writePin(PIN_EPD_CS, false);
    panelSpi.transfer(command);
    writePin(PIN_EPD_CS, true);
}

void Panel::sendData(std::uint8_t data) {
    writePin(PIN_EPD_DC, true);
    writePin(PIN_EPD_CS, false);
    panelSpi.transfer(data);
    writePin(PIN_EPD_CS, true);
}

void Panel::sendData(std::initializer_list<std::uint8_t> data) {
    for (std::uint8_t byte : data) {
        sendData(byte);
    }
}

void Panel::sendDataBulk(std::span<const std::uint8_t> data) {
#if EPD_BULK_TRANSFER
    /*
     * POWER: CS held low across the burst. Waveshare's reference driver raises
     * it between every byte, which for 120,000 bytes triples the time the
     * panel and the CPU are both awake at ~45 mA.
     */
    writePin(PIN_EPD_DC, true);
    writePin(PIN_EPD_CS, false);
    panelSpi.writeBytes(data.data(), data.size());
    writePin(PIN_EPD_CS, true);
#else
    for (std::uint8_t byte : data) {
        sendData(byte);
    }
#endif
}

bool Panel::waitUntilIdle() {
    const std::uint32_t start = millis();
    while (digitalRead(PIN_EPD_BUSY) == LOW) {
        if (millis() - start > EPD_BUSY_TIMEOUT_MS) {
            /*
             * POWER: a wedged panel must not hold us awake. Give up and let
             * the caller sleep; e-ink keeps whatever was already on screen.
             */
            return false;
        }
        delay(10);
    }
    delay(200);
    return true;
}

bool Panel::refresh() {
    sendCommand(0x04);  // POWER_ON
    if (!waitUntilIdle()) {
        return false;
    }
    delay(200);

    sendCommand(0x06);
    sendData({0x6F, 0x1F, 0x17, 0x27});
    delay(200);

    sendCommand(0x12);  // DISPLAY_REFRESH
    sendData(0x00);
    if (!waitUntilIdle()) {
        return false;
    }

    sendCommand(0x02);  // POWER_OFF
    sendData(0x00);
    const bool ok = waitUntilIdle();
    delay(200);
    return ok;
}

bool Panel::displayFile(fs::FS& filesystem, std::string_view path, RowOverlay overlay) {
    if (!initialized_) {
        return false;
    }

    File file = filesystem.open(String(path.data(), path.size()), FILE_READ);
    if (!file) {
        return false;
    }
    if (file.size() != PANEL_BYTES) {
        /*
         * A short file means an interrupted download that somehow reached the
         * manifest. Refuse rather than render half a wedding.
         */
        file.close();
        return false;
    }

    /*
     * POWER: one row at a time. 120,000 bytes would fit in SRAM, but streaming
     * means no allocation and the panel starts accepting data ~200 ms sooner.
     */
    static PanelRow row;

    sendCommand(0x10);
    for (std::uint16_t y = 0; y < PANEL_HEIGHT; y++) {
        if (file.read(row.data(), row.size()) != static_cast<int>(row.size())) {
            file.close();
            return false;
        }
        if (overlay != nullptr) {
            overlay(y, row);
        }
        sendDataBulk(row);
    }
    file.close();

    return refresh();
}

bool Panel::displayGenerated(RowGenerator generate) {
    if (!initialized_ || generate == nullptr) {
        return false;
    }

    static PanelRow row;

    sendCommand(0x10);
    for (std::uint16_t y = 0; y < PANEL_HEIGHT; y++) {
        generate(y, row);
        sendDataBulk(row);
    }
    return refresh();
}

bool Panel::displaySolid(Ink ink) {
    if (!initialized_) {
        return false;
    }

    static PanelRow row;
    const auto packed = static_cast<std::uint8_t>((ink << 4) | ink);
    row.fill(packed);

    sendCommand(0x10);
    for (std::uint16_t y = 0; y < PANEL_HEIGHT; y++) {
        sendDataBulk(row);
    }
    return refresh();
}

}  // namespace polaroid
