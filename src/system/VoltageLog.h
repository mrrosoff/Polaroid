#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include <cstdint>

#include "Config.h"
#include "system/Log.h"

namespace polaroid::vlog {

/*
 * One battery sample per wake, written to flash because reading the cell over
 * USB measures the charger: three samples eleven seconds apart once read 4229,
 * 4147 and 4143 mV, and the low one was the only one taken with the cable out.
 * `host` marks the contaminated ones.
 *
 * Fit a slope through the host=0 rows rather than differencing two endpoints --
 * one reading carries ~4.8 mV of noise, a day of them resolves ~0.15 mA -- and
 * compare slopes only within the same voltage band, since a LiPo's mV per mAh
 * changes across the curve.
 */

/*
 * POWER: one page program per wake, about 0.03 mAh/day against 10.5.
 */
inline void append(std::uint32_t bootCount, std::uint64_t rtcMs, std::uint16_t millivolts,
                   char wake, bool hostAttached) {
    /*
     * Rotate rather than grow. 50 photos leave 422,528 B and a download stages
     * 120,000 of it, so an unbounded log would break replacement eventually.
     */
    File probe = LittleFS.open(config::VLOG_PATH, FILE_READ);
    const bool full = probe && probe.size() >= config::VLOG_MAX_BYTES;
    if (probe) {
        probe.close();
    }
    if (full) {
        LittleFS.remove(config::VLOG_PREV_PATH);
        LittleFS.rename(config::VLOG_PATH, config::VLOG_PREV_PATH);
    }

    File f = LittleFS.open(config::VLOG_PATH, FILE_APPEND);
    if (!f) {
        logf("log", "append failed");
        return;
    }
    char line[64];
    const int n =
        snprintf(line, sizeof(line), "%lu,%llu,%u,%c,%d\n", static_cast<unsigned long>(bootCount),
                 static_cast<unsigned long long>(rtcMs), millivolts, wake, hostAttached ? 1 : 0);
    if (n > 0) {
        f.write(reinterpret_cast<const std::uint8_t*>(line), static_cast<std::size_t>(n));
    }
    f.close();
}

/*
 * Endpoints only. A full dump at the cap is six seconds of scrolling every time
 * the cable goes in, burying the lines around it.
 */
inline void summary() {
    std::uint32_t rows = 0;
    std::uint64_t firstMs = 0, lastMs = 0;
    std::uint32_t firstMv = 0, lastMv = 0;

    for (const char* path : {config::VLOG_PREV_PATH, config::VLOG_PATH}) {
        File f = LittleFS.open(path, FILE_READ);
        if (!f) {
            continue;
        }
        char line[64];
        while (f.available()) {
            const std::size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            unsigned long boot = 0, ms = 0;
            unsigned mv = 0;
            if (sscanf(line, "%lu,%lu,%u,", &boot, &ms, &mv) != 3) {
                continue;
            }
            if (rows == 0) {
                firstMs = ms;
                firstMv = mv;
            }
            lastMs = ms;
            lastMv = mv;
            rows++;
        }
        f.close();
    }

    if (rows == 0) {
        logf("log", "empty");
        return;
    }
    logf("log", "%lu rows, %u -> %u mV over %.1f h  (d = dump, c = clear)",
         static_cast<unsigned long>(rows), firstMv, lastMv,
         static_cast<double>(lastMs - firstMs) / 3600000.0);
}

inline void dumpOne(const char* path) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        return;
    }
    std::uint8_t buf[256];
    while (f.available()) {
        Serial.write(buf, f.read(buf, sizeof(buf)));
    }
    f.close();
}

/*
 * Order by rtcMs, not boot: a reflash resets the counter while the RTC clock
 * keeps running. Oldest file first, so the two read as one series.
 */
inline void dump() {
    logf("log", "dump begin");
    Serial.println("boot,rtc_ms,mv,wake,host");
    dumpOne(config::VLOG_PREV_PATH);
    dumpOne(config::VLOG_PATH);
    logf("log", "dump end");
}

inline void clear() {
    LittleFS.remove(config::VLOG_PREV_PATH);
    LittleFS.remove(config::VLOG_PATH);
    logf("log", "cleared");
}

}  // namespace polaroid::vlog
