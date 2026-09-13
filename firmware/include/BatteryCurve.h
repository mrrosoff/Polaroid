#pragma once

#include <stdint.h>

#include "Config.h"

namespace polaroid {

/*
 * A LiPo sits near 3.7 V for most of its life and then falls off a cliff, so
 * mapping voltage linearly onto 0-100% reports "half full" for about six weeks
 * and then dies in three days. These knots are a piecewise fit to the real
 * curve.
 *
 * Known wrong above 4.0 V, and it does not matter. Measured against 150 hours
 * of hourly samples, the percentage falls 3.6x faster at 4.15 V than at 4.05 V
 * where a correct curve would be straight: the steep drop off a full charge
 * costs very little charge, and these knots read it as proportional loss.
 *
 * It does not matter because the only consumer is
 * `critical = percent <= CRITICAL_BATTERY_PERCENT`, which at 5% lands near
 * 3.37 V, on the cliff below. Nothing displays the number. Fixing the top would
 * change no behaviour, and the cliff is the part no run has reached yet with
 * trustworthy instrumentation -- see the voltage log in the README.
 */
inline uint8_t voltageToPercent(float volts) {
    using namespace config;

    if (volts >= VBAT_FULL_V) {
        return 100;
    }
    if (volts <= VBAT_EMPTY_V) {
        return 0;
    }

    // 4.15 -> 100%, 3.75 -> 40%: the long flat top, most of the runtime.
    if (volts >= VBAT_NOMINAL_V) {
        float t = (volts - VBAT_NOMINAL_V) / (VBAT_FULL_V - VBAT_NOMINAL_V);
        return static_cast<uint8_t>(40.0f + t * 60.0f);
    }

    // 3.75 -> 40%, 3.50 -> 15%: the shoulder.
    if (volts >= VBAT_LOW_V) {
        float t = (volts - VBAT_LOW_V) / (VBAT_NOMINAL_V - VBAT_LOW_V);
        return static_cast<uint8_t>(15.0f + t * 25.0f);
    }

    // 3.50 -> 15%, 3.30 -> 0%: the cliff. Days, not weeks.
    float t = (volts - VBAT_EMPTY_V) / (VBAT_LOW_V - VBAT_EMPTY_V);
    return static_cast<uint8_t>(t * 15.0f);
}

}  // namespace polaroid
