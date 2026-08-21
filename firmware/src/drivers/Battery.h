#pragma once

#include <Arduino.h>

#include "BatteryCurve.h"
#include "Config.h"

namespace polaroid {

struct BatteryReading {
    float volts;
    uint8_t percent;
    bool low;
    bool critical;
};

BatteryReading readBattery();

}  // namespace polaroid
