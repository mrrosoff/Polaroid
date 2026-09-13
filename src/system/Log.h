#pragma once

#include <Arduino.h>

#include <esp_private/esp_clk.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace polaroid {

/*
 * POWER: the `!Serial` guard is the whole point. On battery no host answers, so
 * this returns before touching the port -- an unguarded CDC write blocks until
 * its timeout on every wake.
 *
 * Uptime counts through deep sleep and places a wake among the others;
 * seconds-since-boot restart each wake and show how long a step took. Neither
 * is wall-clock: nothing here knows the date.
 *
 * The tag is a fixed-width column so a capture scans down it.
 */
inline void logf(const char* tag, const char* format, ...) {
    if (!Serial) {
        return;
    }
    va_list args;
    va_start(args, format);
    char line[160];
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    const std::uint64_t up = esp_clk_rtc_time() / 1000000ULL;
    Serial.printf("[%3luh%02lum %6.2fs] %-7s | %s\n", static_cast<unsigned long>(up / 3600),
                  static_cast<unsigned long>((up % 3600) / 60), millis() / 1000.0f, tag, line);
}

}  // namespace polaroid
