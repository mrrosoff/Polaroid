#include "Battery.h"

using namespace config;

namespace polaroid {

BatteryReading readBattery() {
    analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);

    /*
     * Eight samples. The ADC on the S3 is noisy enough that a single read can
     * swing a couple of percent, and this costs microseconds.
     */
    uint32_t total = 0;
    for (int i = 0; i < 8; i++) {
        total += analogReadMilliVolts(PIN_VBAT_SENSE);
    }

    float volts = (total / 8.0f) / 1000.0f * VBAT_DIVIDER_RATIO * VBAT_ADC_CALIBRATION;
    uint8_t percent = voltageToPercent(volts);

    return BatteryReading{
        .volts = volts,
        .percent = percent,
        .critical = percent <= CRITICAL_BATTERY_PERCENT,
    };
}

}  // namespace polaroid
