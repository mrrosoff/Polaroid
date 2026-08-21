// Wiring bring-up only. Built by `pio run -e wiring-test`, which swaps this in
// for main.cpp — no filesystem, no network. It drives the real Panel and the
// real I2C bus, so a pass exercises the same init sequence the firmware ships.
//
// What the pattern tells you:
//   red band along the top   - row order and orientation are right way up
//   six bars, left to right  - black white yellow red blue green
//   bars evenly wide, sharp  - 4bpp packing and the nibble map are correct
//   a bar in the wrong hue   - data line swapped, or 0x4 crept into the map
//   sheared or smeared bars  - SPI clock too fast, or CS released mid-transfer

#include <Arduino.h>
#include <Wire.h>

#include <cstdint>

#include "drivers/Battery.h"
#include "Config.h"
#include "Overlay.h"
#include "drivers/Panel.h"
#include <span>

namespace {

constexpr std::uint16_t BAR_COUNT = 6;
constexpr config::Ink BARS[BAR_COUNT] = {
    config::INK_BLACK, config::INK_WHITE, config::INK_YELLOW,
    config::INK_RED,   config::INK_BLUE,  config::INK_GREEN,
};

constexpr std::uint16_t TOP_BAND_ROWS = 20;

constexpr std::uint8_t LIS3DH_WHO_AM_I = 0x0F;
constexpr std::uint8_t LIS3DH_WHO_AM_I_EXPECTED = 0x33;

void testPattern(std::uint16_t row, std::span<std::uint8_t> rowBytes) {
    if (row < TOP_BAND_ROWS) {
        for (std::uint16_t x = 0; x < config::PANEL_WIDTH; ++x) {
            polaroid::setPixel(rowBytes, x, config::INK_RED);
        }
        return;
    }

    for (std::uint16_t x = 0; x < config::PANEL_WIDTH; ++x) {
        const std::uint16_t bar =
            static_cast<std::uint16_t>(x * BAR_COUNT / config::PANEL_WIDTH);
        polaroid::setPixel(rowBytes, x, BARS[bar]);
    }
}

// Is BUSY driven? Pull it both ways and see if it follows. A line the panel is
// holding ignores the internal pull; a disconnected or unpowered one tracks it.
bool busyIsDriven() {
    pinMode(config::PIN_EPD_BUSY, INPUT_PULLUP);
    delay(10);
    const int pulledUp = digitalRead(config::PIN_EPD_BUSY);

    pinMode(config::PIN_EPD_BUSY, INPUT_PULLDOWN);
    delay(10);
    const int pulledDown = digitalRead(config::PIN_EPD_BUSY);

    pinMode(config::PIN_EPD_BUSY, INPUT);
    Serial.printf("      pullup=%d pulldown=%d\n", pulledUp, pulledDown);
    return !(pulledUp == 1 && pulledDown == 0);
}

// Panel::begin() drives the gate HIGH unconditionally. If the part is
// P-channel that turns the rail OFF, which from the BUSY side looks identical
// to a missing wire. Try it both ways before blaming the solder.
void probePanelRail() {
    pinMode(config::PIN_EPD_PWR, OUTPUT);

    Serial.printf("      BUSY is GPIO%d, PWR gate is GPIO%d\n", config::PIN_EPD_BUSY,
                  config::PIN_EPD_PWR);

    digitalWrite(config::PIN_EPD_PWR, HIGH);
    delay(100);
    Serial.println("      gate HIGH:");
    const bool drivenHigh = busyIsDriven();

    digitalWrite(config::PIN_EPD_PWR, LOW);
    delay(100);
    Serial.println("      gate LOW:");
    const bool drivenLow = busyIsDriven();

    // Leave the rail in whichever state produced a live panel.
    digitalWrite(config::PIN_EPD_PWR, (drivenLow && !drivenHigh) ? LOW : HIGH);
    delay(100);

    if (drivenHigh) {
        Serial.println("ok    panel drives BUSY with the gate HIGH, as the firmware assumes");
    } else if (drivenLow) {
        Serial.println("FAIL  BUSY is only driven with the gate LOW - the gate is inverted.");
        Serial.println("      PIN_EPD_PWR needs active-low polarity in Panel::begin.");
    } else {
        Serial.println("FAIL  BUSY floats either way - not landed, or no power at the panel.");
    }
}

// Check the lines before handing them to Wire. A bus with no pull-ups, or one
// a device is holding down, makes endTransmission() block rather than NACK —
// which looks like a hang, not a missing part.
bool i2cLinesIdle() {
    pinMode(config::PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(config::PIN_I2C_SCL, INPUT_PULLUP);
    delay(10);
    const int sdaPulled = digitalRead(config::PIN_I2C_SDA);
    const int sclPulled = digitalRead(config::PIN_I2C_SCL);

    // With the internal pulls off, only external resistors can hold these high.
    // The internal ones are ~45k and read high either way, so they prove
    // nothing about whether the breakout is actually on the bus.
    pinMode(config::PIN_I2C_SDA, INPUT);
    pinMode(config::PIN_I2C_SCL, INPUT);
    delay(10);
    const int sdaBare = digitalRead(config::PIN_I2C_SDA);
    const int sclBare = digitalRead(config::PIN_I2C_SCL);

    Serial.printf("      SDA=GPIO%d pulled=%d bare=%d\n", config::PIN_I2C_SDA, sdaPulled,
                  sdaBare);
    Serial.printf("      SCL=GPIO%d pulled=%d bare=%d\n", config::PIN_I2C_SCL, sclPulled,
                  sclBare);

    if (sdaPulled == 0 || sclPulled == 0) {
        Serial.println("FAIL  a line is stuck low - shorted, or a device is holding the bus");
        return false;
    }
    if (sdaBare == 0 && sclBare == 0) {
        Serial.println("FAIL  no external pull-ups - the LIS3DH breakout is not on this bus");
        Serial.println("      (its 10k pull-ups would hold both lines high on their own)");
        return false;
    }

    Serial.println("ok    external pull-ups present, bus is idle");
    return true;
}

// The divider is 2 x 1 MΩ off B+, so the ADC sees half the cell. An
// unconnected D0 floats and reads near zero, which is the failure this
// distinguishes from a genuinely flat battery.
// The divider reads correct on a meter but the configured pin sees nothing, so
// find out which pin it actually landed on. ADC1 is GPIO1-10; everything else
// on this board is a digital signal that should read near 0 or 3.3 V, not a
// half-cell voltage.
void scanAdcPins() {
    Serial.println("      scanning ADC1 for the divider:");
    for (int pin = 1; pin <= 10; ++pin) {
        pinMode(pin, INPUT);
        delay(5);
        const int mv = analogReadMilliVolts(pin);
        const char* note = "";
        if (pin == config::PIN_VBAT_SENSE) {
            note = "  <- PIN_VBAT_SENSE";
        } else if (mv > 1200 && mv < 2400) {
            note = "  <- looks like a half-cell divider";
        }
        Serial.printf("      GPIO%-2d  %4d mV%s\n", pin, mv, note);
    }
}

// Charge the pin, let go, and see where it lands. A pin tied to the divider is
// dragged back to the midpoint through 500k in microseconds; a floating pin has
// only leakage to move it and holds the charge for many milliseconds. So if
// both directions settle to the same voltage the wire is connected, and if they
// stay where they were pushed it is not. No meter, no watching required.
bool senseWireIsConnected() {
    pinMode(config::PIN_VBAT_SENSE, OUTPUT);
    digitalWrite(config::PIN_VBAT_SENSE, LOW);
    delay(5);
    pinMode(config::PIN_VBAT_SENSE, INPUT);
    delay(20);
    const int afterLow = analogReadMilliVolts(config::PIN_VBAT_SENSE);

    pinMode(config::PIN_VBAT_SENSE, OUTPUT);
    digitalWrite(config::PIN_VBAT_SENSE, HIGH);
    delay(5);
    pinMode(config::PIN_VBAT_SENSE, INPUT);
    delay(20);
    const int afterHigh = analogReadMilliVolts(config::PIN_VBAT_SENSE);

    Serial.printf("      released from LOW: %d mV, from HIGH: %d mV\n", afterLow, afterHigh);

    const int spread = afterHigh - afterLow;
    if (spread < 300) {
        Serial.printf("ok    both settle to ~%d mV - the wire IS on GPIO%d\n",
                      (afterLow + afterHigh) / 2, config::PIN_VBAT_SENSE);
        return true;
    }

    Serial.println("FAIL  the pin keeps whatever charge it was given, so nothing is");
    Serial.printf("      pulling it anywhere: the junction is NOT reaching GPIO%d.\n",
                  config::PIN_VBAT_SENSE);
    Serial.println("      The divider itself is fine - it is the wire to the pad.");
    return false;
}

void probeBattery() {
    const polaroid::BatteryReading reading = polaroid::readBattery();
    const float atPin = reading.volts / config::VBAT_DIVIDER_RATIO;

    Serial.printf("      VBAT=%.3f V (%.3f V at GPIO%d), %u%%%s%s\n", reading.volts, atPin,
                  config::PIN_VBAT_SENSE, reading.percent, reading.low ? " LOW" : "",
                  reading.critical ? " CRITICAL" : "");

    if (reading.volts >= 1.0f && reading.volts <= 4.4f) {
        Serial.println("ok    in range; meter B+ and correct VBAT_ADC_CALIBRATION if it differs");
        return;
    }

    // Only worth the noise when the reading is wrong. A high-value divider is
    // invisible to a meter on the midpoint — connected or not, it reads the
    // same — so these two are what actually localise the fault.
    if (!senseWireIsConnected()) {
        return;
    }
    scanAdcPins();

    if (reading.volts < 1.0f) {
        // Distinguish a missing cell from a missing divider. With the internal
        // pull-up (~45k) enabled, R2's 1M to ground drags the pin to about
        // 3.16 V; a pin with nothing attached sits at the full 3.3 V. That
        // says whether the resistors are landed, whatever the cell is doing.
        pinMode(config::PIN_VBAT_SENSE, INPUT_PULLUP);
        delay(20);
        const int pulledMv = analogReadMilliVolts(config::PIN_VBAT_SENSE);
        pinMode(config::PIN_VBAT_SENSE, INPUT);
        delay(20);

        Serial.printf("      with pull-up: %d mV\n", pulledMv);
        if (pulledMv > 3250) {
            Serial.println("FAIL  pin floats to rail - nothing is attached to D0 at all");
            Serial.println("      the divider midpoint is not reaching the pin");
        } else if (pulledMv > 2500) {
            Serial.println("ok    divider is landed (R2 pulls the pin below the rail)");
            Serial.println("FAIL  but no cell voltage - check B+ to GND with a meter");
        } else {
            Serial.println("FAIL  pin is held low - R2 may be shorted, or D0 is on GND");
        }
    } else {
        Serial.println("FAIL  reads above a full cell - is D0 on B+ directly, without R1?");
    }
}

void probeAccelerometer() {
    if (!i2cLinesIdle()) {
        return;
    }

    Wire.begin(config::PIN_I2C_SDA, config::PIN_I2C_SCL);
    // Without this a stalled bus blocks for a second per address, and a full
    // scan never returns.
    Wire.setTimeOut(20);
    delay(10);

    // Only the two addresses the LIS3DH can take. A full 112-address sweep
    // cannot finish if the peripheral stalls, and the print before each attempt
    // is what shows which address it wedged on.
    int found = 0;
    for (std::uint8_t addr : {std::uint8_t{0x18}, std::uint8_t{0x19}}) {
        Serial.printf("      trying 0x%02X\n", addr);
        Wire.beginTransmission(addr);
        const std::uint8_t result = Wire.endTransmission();
        Serial.printf("      0x%02X -> %d\n", addr, result);
        if (result == 0) {
            ++found;
        }
    }
    if (found == 0) {
        // Code 5 is a timeout, not a NACK. A missing chip NACKs (2); a timeout
        // with healthy pull-ups means the clock and data are not reaching it —
        // which is what SDA and SCL swapped looks like from here.
        Serial.println("      no answer; retrying with SDA and SCL swapped");
        Wire.end();
        delay(10);
        Wire.begin(config::PIN_I2C_SCL, config::PIN_I2C_SDA);
        Wire.setTimeOut(20);
        delay(10);

        for (std::uint8_t addr : {std::uint8_t{0x18}, std::uint8_t{0x19}}) {
            Wire.beginTransmission(addr);
            const std::uint8_t result = Wire.endTransmission();
            Serial.printf("      swapped 0x%02X -> %d\n", addr, result);
            if (result == 0) {
                Serial.println("FAIL  the accelerometer answers with SDA and SCL SWAPPED.");
                Serial.printf("      PIN_I2C_SDA and PIN_I2C_SCL are reversed: SDA is on "
                              "GPIO%d, SCL on GPIO%d\n",
                              config::PIN_I2C_SCL, config::PIN_I2C_SDA);
                return;
            }
        }
        Serial.println("FAIL  no answer either way - check VIN, GND and both signal wires");
        return;
    }

    Wire.beginTransmission(config::ACCEL_I2C_ADDRESS);
    Wire.write(LIS3DH_WHO_AM_I);
    if (Wire.endTransmission(false) != 0) {
        Serial.printf("FAIL  no LIS3DH at 0x%02X (SDO/SA0 picks 0x18 vs 0x19)\n",
                      config::ACCEL_I2C_ADDRESS);
        return;
    }
    Wire.requestFrom(static_cast<std::uint8_t>(config::ACCEL_I2C_ADDRESS),
                     static_cast<std::uint8_t>(1));
    const std::uint8_t who = Wire.available() ? Wire.read() : 0;

    if (who == LIS3DH_WHO_AM_I_EXPECTED) {
        Serial.printf("ok    LIS3DH at 0x%02X, WHO_AM_I=0x%02X\n", config::ACCEL_I2C_ADDRESS,
                      who);
    } else {
        Serial.printf("FAIL  WHO_AM_I=0x%02X, expected 0x%02X\n", who,
                      LIS3DH_WHO_AM_I_EXPECTED);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    // USB CDC enumerates after boot; without this the first prints are lost.
    delay(2000);
    Serial.println();
    Serial.println("polaroid wiring test");

    Serial.println("--- battery ---");
    probeBattery();

    Serial.println("--- accelerometer ---");
    probeAccelerometer();

    Serial.println("--- panel rail ---");
    probePanelRail();

    Serial.println("--- panel ---");
    polaroid::Panel panel;
    const std::uint32_t started = millis();
    const bool up = panel.begin();
    const std::uint32_t elapsed = millis() - started;

    if (!up) {
        // begin() waits for idle twice, once after reset and once after the
        // init sequence, each with the same timeout. Roughly one timeout means
        // the panel never came out of reset; roughly two means it accepted the
        // reset and then wedged on the init sequence.
        Serial.printf("FAIL  panel.begin() after %lu ms\n", static_cast<unsigned long>(elapsed));
        if (elapsed < config::EPD_BUSY_TIMEOUT_MS + 5000) {
            Serial.println("      timed out on the FIRST wait - panel never released BUSY");
            Serial.println("      after reset. Check RST and the panel rail.");
        } else {
            Serial.println("      reset was accepted; it wedged on the init sequence.");
            Serial.println("      Check MOSI, SCK, CS and DC.");
        }
        return;
    }

    Serial.printf("ok    panel init done after %lu ms, streaming pattern\n",
                  static_cast<unsigned long>(elapsed));

    const std::uint32_t refreshStart = millis();
    const bool sent = panel.displayGenerated(testPattern);
    const std::uint32_t refreshMs = millis() - refreshStart;

    if (!sent) {
        Serial.printf("FAIL  displayGenerated() after %lu ms\n",
                      static_cast<unsigned long>(refreshMs));
        return;
    }

    // A real refresh holds BUSY low for 15-35 s. Returning sooner means every
    // waitUntilIdle() passed on a line that was never asserted - the panel was
    // not listening, and the "success" is meaningless.
    if (refreshMs < 10000) {
        Serial.printf("FAIL  refresh returned in %lu ms; a real one takes 15-35 s.\n",
                      static_cast<unsigned long>(refreshMs));
        Serial.println("      BUSY never asserted, so the panel ignored the whole sequence.");
        return;
    }

    Serial.printf("ok    refresh took %lu ms; pattern is on the panel\n",
                  static_cast<unsigned long>(refreshMs));
}

void loop() {}
