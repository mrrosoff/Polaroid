#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include <cstdint>

#include "Config.h"

namespace polaroid::vlog {

/*
 * One battery sample per wake, written to flash instead of read over USB.
 *
 * Reading the cell over the cable measures the charger. The ADC divider sits
 * on the battery terminal, and a terminal on a charger reads whatever the
 * charger holds it at: on 2026-08-31 three samples eleven seconds apart read
 * 4229, 4147 and 4143 mV, and the low one was the only sample taken with the
 * cable out. That 86 mV spread is roughly eighteen times the ADC's own noise,
 * which is why a week of plug-in-and-shake readings could not measure a
 * discharge.
 *
 * So the sample is taken on battery and read back later. `host` marks the ones
 * taken with USB attached; they are contaminated upward and should be dropped
 * rather than trusted.
 *
 * Fit a slope through the host=0 rows rather than differencing two endpoints.
 * Single readings carry about 4.8 mV of ADC noise, but a day of hourly points
 * resolves the drain to roughly +/-0.5 mA -- enough to tell a 0.44 mA budget
 * from the milliamps actually being drawn, and enough to date the onset of
 * anything episodic, which two endpoints can never do.
 */

/*
 * POWER: one small append per wake. A page program is a few milliseconds at
 * roughly 20 mA, so about 0.03 mAh/day against a 10.5 mAh/day budget -- under
 * a third of a percent, and far below the resolution of what it measures.
 */
inline void append(std::uint32_t bootCount, std::uint64_t rtcMs, std::uint16_t millivolts,
                   char wake, bool hostAttached) {
    /*
     * Rotate rather than grow. The photo cap is 50 of the 53 that fit because
     * a download stages a full framebuffer before renaming over the old one,
     * and that staging space is the headroom an unbounded log would eat:
     * 50 photos leave 422,528 B, and two full logs take 131,072 of it, which
     * still clears the 120,000 a download needs. A log that quietly broke
     * photo replacement after a year would be indistinguishable from the frame
     * being full.
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

inline void dumpOne(const char* path) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        return;
    }
    Serial.printf("vlog %s: %u bytes\n", path, static_cast<unsigned>(f.size()));
    std::uint8_t buf[256];
    while (f.available()) {
        Serial.write(buf, f.read(buf, sizeof(buf)));
    }
    f.close();
}

/*
 * rtcMs counts through deep sleep but restarts at zero when the cell is
 * disconnected or the board is power-cycled, and bootCount restarts with it.
 * So a run is a block of rows sharing a boot sequence, and rtcMs is time since
 * that run began. Oldest file first, so the two read as one series.
 */
inline void dump() {
    Serial.println("vlog: boot,rtc_ms,mv,wake,host");
    dumpOne(config::VLOG_PREV_PATH);
    dumpOne(config::VLOG_PATH);
    Serial.println("vlog: end");
}

inline void clear() {
    LittleFS.remove(config::VLOG_PREV_PATH);
    LittleFS.remove(config::VLOG_PATH);
    Serial.println("vlog: cleared");
}

}  // namespace polaroid::vlog
