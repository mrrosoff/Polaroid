// Shake calibration bench. Built by `pio run -e shake-test`, which swaps this
// in for main.cpp. No panel, no network — it arms the real detectors through
// Motion::begin() and reports what Motion::classifyWakeEvent() decides, so the
// thresholds in Config.h can be tuned against actual hands.
//
// Shake it: expect SHAKE. Leave it still: expect silence. Raise
// ACTIVITY_THRESHOLD if knocks register, lower it if real shakes are missed.

#include <Arduino.h>

#include <cstdint>

#include "Config.h"
#include "drivers/Motion.h"

namespace {

polaroid::Motion motion;

std::uint32_t shakes = 0;
std::uint32_t spurious = 0;
std::uint32_t suppressed = 0;
std::uint32_t lastEventMs = 0;

const char* name(polaroid::MotionEvent event) {
    switch (event) {
        case polaroid::MotionEvent::Shake: return "SHAKE";
        default: return "none";
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println();
    Serial.println("polaroid shake test");

    if (!motion.begin()) {
        Serial.printf("FAIL  no LIS3DH at 0x%02X\n", config::ACCEL_I2C_ADDRESS);
        return;
    }

    Serial.printf("ok    armed. INT1 on GPIO%d, activity threshold %u (~%u mg), duration %u\n",
                  config::PIN_ACCEL_INT1, config::ACTIVITY_THRESHOLD,
                  config::ACTIVITY_THRESHOLD * 31u, config::ACTIVITY_DURATION);
    Serial.println("      shake it, then leave it still and check nothing fires");

    // Clear anything latched from power-on so the first real event is real.
    motion.armForSleep();
}

void loop() {
    if (digitalRead(config::PIN_ACCEL_INT1) == LOW) {
        return;
    }

    // The firmware debounces across deep sleep for the same reason: one shake
    // bounces into several interrupts, and each would cost a panel refresh.
    const std::uint32_t now = millis();
    if (lastEventMs != 0 && now - lastEventMs < config::MOTION_DEBOUNCE_MS) {
        // Distinguishes a shake the detector missed from one the debounce ate:
        // silence here means the threshold is too high, this line means it is
        // working and MOTION_DEBOUNCE_MS is what collapsed the burst.
        ++suppressed;
        Serial.printf("%8lu ms  (debounced, %lu ms since last)   suppressed=%lu\n",
                      static_cast<unsigned long>(now),
                      static_cast<unsigned long>(now - lastEventMs),
                      static_cast<unsigned long>(suppressed));
        motion.armForSleep();
        return;
    }
    lastEventMs = now;

    const polaroid::MotionEvent event = motion.classifyWakeEvent();
    switch (event) {
        case polaroid::MotionEvent::Shake: ++shakes; break;
        default: ++spurious; break;
    }

    Serial.printf("%8lu ms  %-6s   shakes=%lu spurious=%lu\n",
                  static_cast<unsigned long>(now), name(event),
                  static_cast<unsigned long>(shakes), static_cast<unsigned long>(spurious));

    // classifyWakeEvent already read INT1_SRC, which is what releases the
    // latch. If the line is still high the detector re-fired while we were
    // printing, and the next pass catches it.
}
