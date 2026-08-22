#include "State.h"

#include <WiFi.h>
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

    // Measured, not guessed: with the teardown above in place a shake never
    // woke the device, and with it skipped the wake counter climbed on every
    // shake. Stopping and deinitialising the radio releases its power
    // management locks, which leaves the RTC peripheral domain configured off,
    // and ext0 runs on that domain. Assert it back on before arming.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // POWER: every unused pin gets an explicit pull. A floating CMOS input
    // oscillates around its threshold and burns current that is very hard to
    // attribute later.
    for (int pin : UNUSED_PINS) {
        pinMode(pin, INPUT_PULLDOWN);
    }

    // INT1 is push-pull active-high from the LIS3DH, so ext0 waits for a 1.
    // Motion::armForSleep must have already cleared the latch and waited for
    // the line to settle, or this fires the instant we sleep.
    // No rtc_gpio_* setup here. The LIS3DH drives INT1 push-pull, so it needs
    // no internal pull, and touching the pad outside of
    // esp_sleep_enable_ext0_wakeup's own configuration stopped the wake from
    // firing at all. Verified by bisecting against a minimal sleep test that
    // armed ext0 and nothing else.
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_ACCEL_INT1), 1);

    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);

    // POWER: the RTC peripheral domain stays on, and that is deliberate. ext0
    // runs on it, so powering it down to save tens of µA silently disables the
    // motion wake — the device sleeps, wakes on the hourly timer, and ignores
    // every shake, with nothing in any log to say why. Measured on hardware:
    // with the domain powered down a shake never wakes it; with it on, it
    // wakes every time. Do not add esp_sleep_pd_config here without measuring
    // the sleep current first and re-testing a shake after.

    Serial.flush();
    esp_deep_sleep_start();

    // esp_deep_sleep_start does not return.
    for (;;) {
    }
}

}  // namespace polaroid
