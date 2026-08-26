#pragma once

#include <Arduino.h>

#include "Config.h"

namespace polaroid {

enum class WakeReason : uint8_t {
    ColdBoot,
    Timer,
    Motion,
};

/*
 * Lives in RTC slow memory: survives deep sleep, costs no flash writes, and
 * flash writes are both slow and finite. Nothing here is worth a wear cycle.
 */
enum PanelContent : uint8_t {
    PANEL_PHOTO = 0,
    PANEL_BATTERY_CARD = 1,
    PANEL_NO_PHOTOS_CARD = 2,
};

struct RtcState {
    uint32_t magic;
    uint16_t photoIndex;
    uint16_t photoCount;
    uint32_t bootCount;
    uint32_t secondsSinceSync;
    /*
     * RTC-clock milliseconds at the last accepted motion wake. Zero means none
     * has ever been recorded. Must be an RTC reading, not esp_timer: see
     * motionTooSoon().
     */
    uint64_t lastMotionMs;
    /*
     * What the panel is currently showing, so a wake can tell "this card is
     * already up" from "something else painted over it".
     *
     * One field rather than a flag per card. Two booleans cannot invalidate
     * each other: the battery card would set its flag, a photo would paint
     * over it, the flag would still read "drawn", and the next critical wake
     * would skip the redraw and sleep six hours showing a photo while the
     * device died -- which is the exact silent death the card exists to
     * prevent.
     */
    uint8_t panelShows;
    // Consecutive failed syncs. Drives the retry backoff and the offline icon.
    uint8_t syncFailures;
    uint8_t lastExit;
    uint32_t checksum;
};

constexpr uint32_t RTC_MAGIC = 0x504F4C41;  // 'POLA'

RtcState& rtcState();
void resetRtcState();
bool rtcStateValid();
void commitRtcState();

WakeReason wakeReason();

/*
 * The only way out of setup(). Tears down every rail, pins every unused GPIO,
 * arms ext0 on INT1 plus the refresh timer, and does not return.
 */
[[noreturn]] void sleepUntilNextEvent(uint32_t seconds);

}  // namespace polaroid
