#pragma once

#include <Arduino.h>

#include "Config.h"

namespace polaroid {

enum class WakeReason : uint8_t {
    ColdBoot,
    Timer,
    Motion,
};

enum class Mode : uint8_t {
    Normal,
    Sync,
    Fridge,
    Provision,
};

// Lives in RTC slow memory: survives deep sleep, costs no flash writes, and
// flash writes are both slow and finite. Nothing here is worth a wear cycle.
struct RtcState {
    uint32_t magic;
    uint16_t photoIndex;
    uint16_t photoCount;
    uint32_t bootCount;
    uint32_t secondsSinceSync;
    uint32_t lastMotionMillisSinceBoot;
    uint8_t lowBattery;
    uint8_t provisioned;
    // Set once the "CHARGE ME" card is on the panel. Without it the device
    // would redraw the card on every wake, spending the last of the battery
    // repainting a picture that is already there.
    uint8_t emptyCardDrawn;
    // Consecutive failed syncs. Drives the retry backoff and the offline icon.
    uint8_t syncFailures;
    uint32_t checksum;
};

constexpr uint32_t RTC_MAGIC = 0x504F4C41;  // 'POLA'

RtcState& rtcState();
void resetRtcState();
bool rtcStateValid();
void commitRtcState();

WakeReason wakeReason();

// The only way out of setup(). Tears down every rail, pins every unused GPIO,
// arms ext0 on INT1 plus the refresh timer, and does not return.
[[noreturn]] void sleepUntilNextEvent(uint32_t seconds);

}  // namespace polaroid
