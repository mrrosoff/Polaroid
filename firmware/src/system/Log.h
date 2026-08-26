#pragma once

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

namespace polaroid {

/*
 * POWER: the `!Serial` check is the whole guard, and it is enough. On battery
 * there is no USB host, so HWCDC never reports connected and this returns
 * before touching the port -- an unguarded CDC write blocks until its timeout
 * on every wake. Plugged into a host it logs, which is the only way to see
 * anything on a device with no screen worth reading and no other output.
 */
inline void logf(const char* format, ...) {
    if (!Serial) {
        return;
    }
    va_list args;
    va_start(args, format);
    char line[160];
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    Serial.println(line);
}

}  // namespace polaroid
