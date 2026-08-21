#include "State.h"

#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

using namespace config;

namespace polaroid {

namespace {

RTC_DATA_ATTR RtcState state;

uint32_t computeChecksum(const RtcState& s) {
    // Everything but the checksum field itself. RTC memory survives deep sleep
    // but not a brownout, and a garbage photoIndex is how you end up showing
    // nothing at all.
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

    // POWER: disconnect(true, true) drops the AP and wipes stored config, then
    // the radio is stopped and deinited. Calling only WiFi.disconnect() leaves
    // the PHY powered — about 1 mA that follows you into deep sleep and is
    // invisible without a meter on the rail.
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();

    // POWER: every unused pin gets an explicit pull. A floating CMOS input
    // oscillates around its threshold and burns current that is very hard to
    // attribute later.
    for (int pin : UNUSED_PINS) {
        pinMode(pin, INPUT_PULLDOWN);
    }

    // INT1 is push-pull active-high from the LIS3DH, so ext0 waits for a 1.
    // Motion::armForSleep must have already cleared the latches or this fires
    // the instant we sleep.
    rtc_gpio_pulldown_en(static_cast<gpio_num_t>(PIN_ACCEL_INT1));
    rtc_gpio_pullup_dis(static_cast<gpio_num_t>(PIN_ACCEL_INT1));
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_ACCEL_INT1), 1);

    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);

    // Nothing in the RTC peripheral domain is needed while asleep; leaving it
    // on is worth tens of µA on the S3.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

    Serial.flush();
    esp_deep_sleep_start();

    // esp_deep_sleep_start does not return.
    for (;;) {
    }
}

}  // namespace polaroid
