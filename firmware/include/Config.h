#pragma once

#include <array>
#include <cstdint>

// Every tunable in the firmware lives here. If you are calibrating on the real
// fridge, everything you need is in the Motion section.

namespace config {

// ---------------------------------------------------------------- panel

// Waveshare 4" E Ink Spectra 6 (E6), driver EPD_4in0e. Native portrait.
constexpr uint16_t PANEL_WIDTH = 400;
constexpr uint16_t PANEL_HEIGHT = 600;
constexpr uint32_t PANEL_ROW_BYTES = PANEL_WIDTH / 2;  // 4bpp, two pixels per byte
constexpr uint32_t PANEL_BYTES = PANEL_ROW_BYTES * PANEL_HEIGHT;  // 120,000

// The six inks, as the controller numbers them. There is nothing in between.
enum Ink : uint8_t {
    INK_BLACK = 0x0,
    INK_WHITE = 0x1,
    INK_YELLOW = 0x2,
    INK_RED = 0x3,
    INK_BLUE = 0x5,
    INK_GREEN = 0x6,
};

// Hold CS low across the whole 120 KB data phase instead of toggling it per byte
// the way Waveshare's reference driver does. Same wire protocol, ~3x faster, and
// at 45 mA the difference is worth real days of runtime. Set to 0 if a panel
// turns out to be fussy about it.
#define EPD_BULK_TRANSFER 1

constexpr uint32_t EPD_SPI_HZ = 20'000'000;

// Panel pulls BUSY low while refreshing. 35 s is the far end of the datasheet's
// range; past that we assume the panel is wedged and sleep anyway rather than
// spin on the pin until the battery is gone.
constexpr uint32_t EPD_BUSY_TIMEOUT_MS = 40'000;

// ---------------------------------------------------------------- pins

// Two constraints drive every assignment here:
//   every wake source must be on GPIO0-21, the S3's RTC domain
//   the battery divider must be on ADC1 (GPIO1-10), the ADC that still
//   works while WiFi is up

#if defined(POLAROID_BOARD_XIAO)

// Seeed XIAO ESP32S3. Exactly eleven GPIO broken out and this design needs
// exactly eleven, so there is no slack — D6/D7 are GPIO43/44, which are
// outside the RTC domain and have no ADC, so they get the two signals that
// need neither.
constexpr int PIN_EPD_SCK = 43;   // D6
constexpr int PIN_EPD_MOSI = 44;  // D7
constexpr int PIN_EPD_CS = 9;     // D10
constexpr int PIN_EPD_DC = 8;     // D9
constexpr int PIN_EPD_RST = 7;    // D8
constexpr int PIN_EPD_BUSY = 6;   // D5
constexpr int PIN_EPD_PWR = 5;    // D4

constexpr int PIN_I2C_SCL = 4;     // D3
constexpr int PIN_I2C_SDA = 3;     // D2
constexpr int PIN_ACCEL_INT1 = 2;  // D1, RTC-capable

constexpr int PIN_VBAT_SENSE = 1;  // D0, ADC1_CH0

// Nothing left over. The XIAO's remaining pads are the USB differential pair
// and the internal flash/PSRAM bus, which must not be touched.
constexpr std::array<int, 0> UNUSED_PINS{};

#else

// Generic ESP32-S3 SuperMini.
constexpr int PIN_EPD_SCK = 12;
constexpr int PIN_EPD_MOSI = 11;
constexpr int PIN_EPD_CS = 10;
constexpr int PIN_EPD_DC = 9;
constexpr int PIN_EPD_RST = 8;
constexpr int PIN_EPD_BUSY = 7;
constexpr int PIN_EPD_PWR = 6;

constexpr int PIN_I2C_SCL = 4;
constexpr int PIN_I2C_SDA = 5;
constexpr int PIN_ACCEL_INT1 = 3;

constexpr int PIN_VBAT_SENSE = 2;  // ADC1_CH1

// Broken out, unused, and floating. Pulled explicitly before sleep — a
// floating CMOS input oscillates and the current it burns is nearly impossible
// to find with a meter after the fact.
constexpr std::array UNUSED_PINS{13, 14, 21, 33, 34, 35, 36, 37, 38};

#endif

// POWER: PIN_EPD_PWR gates the panel driver board's rail. See docs/POWER.md —
// it is worth about 30 µA, which is a third of the sleep budget.

// ---------------------------------------------------------------- timing

constexpr uint32_t REFRESH_INTERVAL_SECONDS = 60 * 60;  // hourly

// POWER: this is the dominant term in the whole budget, not sleep current.
// Doubling it to two hours buys roughly 60 extra days. See docs/POWER.md.

constexpr uint32_t SYNC_INTERVAL_SECONDS = 24 * 60 * 60;

// A failed sync retries sooner than a day, then backs off: 1h, 2h, 4h ... up
// to the normal interval. Without this a router outage retries every hour, and
// 24 connect timeouts a day costs more than the entire rest of the budget.
constexpr uint32_t SYNC_RETRY_BASE_SECONDS = 60 * 60;

// Draw the offline icon once this many syncs in a row have failed. Three is
// about seven hours of backoff, so a brief blip never shows.
constexpr uint8_t OFFLINE_ICON_AFTER_FAILURES = 3;

// Caps the backoff shift so it can't overflow.
constexpr uint8_t MAX_SYNC_FAILURES = 8;

// A shake is a deliberate act, so it gets a longer network budget than the
// unattended daily sync — someone is standing there watching.
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15'000;
constexpr uint32_t SYNC_TIMEOUT_MS = 45'000;
constexpr uint32_t HTTP_TIMEOUT_MS = 10'000;

// Captive portal gives up and sleeps rather than sitting at 80 mA forever if
// the couple wanders off mid-setup.
constexpr uint32_t PROVISION_TIMEOUT_MS = 10 * 60 * 1000;

// ---------------------------------------------------------------- motion

// Motion means one thing: shake to sync. There is no gesture to tell apart, so
// the activity detector alone decides, and the threshold is what separates a
// deliberate shake from the frame being knocked. Calibrate with
// `pio run -e shake-test`.

constexpr uint8_t ACCEL_I2C_ADDRESS = 0x18;  // SDO to GND
constexpr uint8_t ACCEL_RANGE_G = 4;

// Threshold is 1/128 of full scale, so at ±4 g one count is about 31 mg. The
// interrupt path is high-pass filtered, so this is measured against movement
// with gravity already removed — a resting frame reads near zero at any angle.
constexpr uint8_t ACTIVITY_THRESHOLD = 40;

// Samples above the threshold before INT1 asserts, in 1/ODR units. At 50 Hz
// each count is 20 ms, so 2 means the movement has to last 40 ms: long enough
// to reject a single sharp tap on the door.
constexpr uint8_t ACTIVITY_DURATION = 2;

// Ignore anything within this long of the last event: hands bounce, and
// without this one shake reads as four, each costing a full sync.
constexpr uint32_t MOTION_DEBOUNCE_MS = 3'000;

// ---------------------------------------------------------------- battery

// 2 x 1 MΩ divider, so the ADC sees half of VBAT.
constexpr float VBAT_DIVIDER_RATIO = 2.0f;

// Measured, not nominal. Correct this once against a multimeter and the
// percentage curve lines up for the rest of the device's life.
constexpr float VBAT_ADC_CALIBRATION = 1.0f;

// LiPo discharge curve is flat from 4.2 down to about 3.5 and then falls off a
// cliff, so a linear percentage is a lie. These are the knots of the curve
// Battery.cpp interpolates between.
constexpr float VBAT_FULL_V = 4.15f;
constexpr float VBAT_NOMINAL_V = 3.75f;
constexpr float VBAT_LOW_V = 3.50f;
constexpr float VBAT_EMPTY_V = 3.30f;

// Show the corner icon below this. Composited onto a refresh we were already
// doing, so it costs nothing extra.
constexpr uint8_t LOW_BATTERY_PERCENT = 15;

// Below this the device stops showing photos and draws the "CHARGE ME" card
// once, then stops refreshing entirely.
//
// The threshold has to leave enough charge to actually finish that last
// refresh — 5% of 2000 mAh is 100 mAh against the ~0.4 mAh a refresh costs,
// so there is enormous margin. Getting this wrong in the other direction is
// what you must avoid: a rail that sags twenty seconds into a refresh leaves
// a half-drawn frame on the panel forever.
constexpr uint8_t CRITICAL_BATTERY_PERCENT = 5;

// Hysteresis for coming back after a charge. Deliberately far above the
// critical threshold: a reading that hovers on the line would otherwise
// redraw the card and resume in a loop, and each of those cycles is a full
// 30 s refresh out of a battery that has nothing left to give.
constexpr uint8_t BATTERY_RECOVERY_PERCENT = 25;

// Once the card is up there is nothing to do but wait for a charger, so wake
// rarely. A wake that only reads the ADC costs microamp-seconds, but there is
// no reason to spend even that hourly.
constexpr uint32_t EMPTY_CHECK_INTERVAL_SECONDS = 6 * 60 * 60;

// ---------------------------------------------------------------- network

constexpr char API_BASE_URL[] = "https://api.maxrosoff.com/polaroid";
constexpr char PROVISION_AP_NAME[] = "Polaroid Setup";

// 50, not the 53 that fit: downloadPhoto stages a full temp file before
// replacing. Must match MAX_PHOTOS in the service's api/common.ts.
constexpr uint8_t MAX_PHOTOS = 50;

constexpr char MANIFEST_PATH[] = "/manifest.json";
constexpr char PHOTO_DIR[] = "/p";

}  // namespace config
