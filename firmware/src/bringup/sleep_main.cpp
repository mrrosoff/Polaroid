// The smallest thing that can answer "does a shake wake this board". No panel,
// no WiFi, no filesystem — deep sleep, an INT1 wake source, and a line printed
// on the way back in.
//
// Built by `pio run -e sleep-min`. Flip the two knobs below to bisect: ext0
// against ext1, and whether the RTC peripheral domain stays powered.

#include <Arduino.h>
#include <esp_sleep.h>

#include <cstdint>

#include "Config.h"
#include "drivers/Motion.h"

// The two things under test.
constexpr bool USE_EXT1 = false;
constexpr bool POWER_DOWN_RTC_PERIPH = false;

namespace {

polaroid::Motion motion;

const char* wakeCause() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
        case ESP_SLEEP_WAKEUP_EXT0: return "EXT0 (motion)";
        case ESP_SLEEP_WAKEUP_EXT1: return "EXT1 (motion)";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "cold boot / reset";
        default: return "other";
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    for (std::uint32_t waited = 0; !Serial && waited < 3000; waited += 50) {
        delay(50);
    }
    delay(300);

    Serial.println();
    Serial.printf("woke: %s\n", wakeCause());
    Serial.printf("config: %s, RTC_PERIPH %s\n", USE_EXT1 ? "ext1" : "ext0",
                  POWER_DOWN_RTC_PERIPH ? "off" : "on");

    if (!motion.begin()) {
        Serial.println("FAIL  no LIS3DH");
        return;
    }

    // Clears the latch and waits for INT1 to settle, so we are not sleeping
    // into an already-asserted line.
    motion.armForSleep();
    Serial.printf("INT1 (GPIO%d) reads %d before sleeping\n", config::PIN_ACCEL_INT1,
                  digitalRead(config::PIN_ACCEL_INT1));
    motion.powerDown();

    if (USE_EXT1) {
        esp_sleep_enable_ext1_wakeup(1ULL << config::PIN_ACCEL_INT1, ESP_EXT1_WAKEUP_ANY_HIGH);
    } else {
        esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(config::PIN_ACCEL_INT1), 1);
    }

    // A short timer so a failed motion wake still comes back on its own and
    // tells us the timer path is fine.
    esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);

    if (POWER_DOWN_RTC_PERIPH) {
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    }

    Serial.println("sleeping now — shake it");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}

void loop() {}
