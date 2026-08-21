#pragma once

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

namespace polaroid {

// POWER: guarded twice. Without POLAROID_BRINGUP this compiles to nothing, and
// even in a bringup build it stays silent unless a USB host has raised DTR —
// an unguarded CDC write blocks until its timeout on every wake.
inline void logf([[maybe_unused]] const char* format, ...) {
#ifdef POLAROID_BRINGUP
    if (!Serial) {
        return;
    }
    va_list args;
    va_start(args, format);
    char line[160];
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    Serial.println(line);
#endif
}

}  // namespace polaroid
