#include "State.h"

#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

using namespace config;

namespace polaroid {

namespace {

RTC_DATA_ATTR RtcState state;

uint32_t computeChecksum(const RtcState& s) {
    /*
     * Everything but the checksum field itself. RTC memory survives deep sleep
     * but not a brownout, and a garbage photoIndex is how you end up showing
     * nothing at all.
     */
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&s);
    size_t length = offsetof(RtcState, checksum);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

}  // namespace

RtcState& rtcState() { return state; }

bool rtcStateValid() {
    return state.magic == RTC_MAGIC && state.checksum == computeChecksum(state);
}

void resetRtcState() {
    memset(&state, 0, sizeof(state));
    state.magic = RTC_MAGIC;
    commitRtcState();
}

void commitRtcState() { state.checksum = computeChecksum(state); }

WakeReason wakeReason() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1: return WakeReason::Motion;
        default: return WakeReason::ColdBoot;
    }
}

[[noreturn]] void sleepUntilNextEvent(uint32_t seconds) {
    commitRtcState();

    /*
     * POWER: disconnect(true, true) drops the AP and wipes stored config, then
     * the radio is stopped and deinited. Calling only WiFi.disconnect() leaves
     * the PHY powered — about 1 mA that follows you into deep sleep and is
     * invisible without a meter on the rail.
     */
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();

    /*
     * Measured, not guessed: with the teardown above in place a shake never
     * woke the device, and with it skipped the wake counter climbed on every
     * shake. Stopping and deinitialising the radio releases its power
     * management locks, which leaves the RTC peripheral domain configured off,
     * and ext0 runs on that domain. Assert it back on before arming.
     */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    /*
     * POWER: a GPIO stops being driven the moment the chip deep sleeps unless
     * it is explicitly held, and two pads here must not float.
     *
     * PIN_EPD_PWR gates the panel driver board's rail. Released, the gate
     * floats and the rail can sit powered for the whole sleep -- which is the
     * one thing this pin exists to prevent.
     *
     * PIN_STATUS_LED is the XIAO's user LED, active low, so a floating pad
     * lights it and leaves it lit until the next wake.
     *
     * Both are driven here rather than relying on whatever ran earlier: not
     * every path into this function has constructed a Panel, so PIN_EPD_PWR
     * may never have been an output at all.
     */
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, LOW);
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);  // active low: high is off

    gpio_hold_en(static_cast<gpio_num_t>(PIN_EPD_PWR));
    gpio_hold_en(static_cast<gpio_num_t>(PIN_STATUS_LED));
    gpio_deep_sleep_hold_en();

    /*
     * POWER: every unused pin gets an explicit pull. A floating CMOS input
     * oscillates around its threshold and burns current that is very hard to
     * attribute later.
     */
    for (int pin : UNUSED_PINS) {
        pinMode(pin, INPUT_PULLDOWN);
    }

    /*
     * INT1 is push-pull active-high, so ext0 waits for a 1 and needs no
     * internal pull. armForSleep must already have settled the line.
     *
     * No rtc_gpio_* setup here: touching the pad outside of
     * esp_sleep_enable_ext0_wakeup's own configuration stopped the wake
     * firing at all.
     */
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_ACCEL_INT1), 1);

    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);

    /*
     * POWER: the RTC peripheral domain stays on, and that is deliberate. ext0
     * runs on it, so powering it down to save tens of µA silently disables the
     * motion wake — the device sleeps, wakes on the hourly timer, and ignores
     * every shake, with nothing in any log to say why. Measured on hardware:
     * with the domain powered down a shake never wakes it; with it on, it
     * wakes every time. Do not add esp_sleep_pd_config here without measuring
     * the sleep current first and re-testing a shake after.
     */

    Serial.flush();
    esp_deep_sleep_start();

    // esp_deep_sleep_start does not return.
    for (;;) {
    }
}

}  // namespace polaroid
