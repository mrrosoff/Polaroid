#include <Arduino.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "Battery.h"
#include "Config.h"
#include "Manifest.h"
#include "Motion.h"
#include "Net.h"
#include "Overlay.h"
#include "Panel.h"
#include "State.h"
#include "StatusCard.h"
#include "Storage.h"

using namespace config;
using namespace polaroid;

namespace {

Storage storage;
Motion motion;
Manifest manifest;
BatteryReading battery;

bool showLowBatteryIcon = false;
bool showOfflineIcon = false;

bool overlayHook(std::uint16_t row, std::span<std::uint8_t> rowBytes) {
    bool touched = false;
    if (showLowBatteryIcon) {
        touched |= lowBatteryOverlay(row, rowBytes);
    }
    if (showOfflineIcon) {
        touched |= offlineOverlay(row, rowBytes);
    }
    return touched;
}

// Renders whatever is at rtcState().photoIndex. Every failure here is
// non-fatal: e-ink holds its last image, so the worst case is that the couple
// looks at yesterday's photo for another hour.
void renderCurrentPhoto() {
    if (manifest.photos.empty()) {
        return;
    }

    RtcState& state = rtcState();
    if (state.photoIndex >= manifest.photos.size()) {
        state.photoIndex = 0;
    }

    std::array<char, 48> path{};
    storage.photoPath(manifest.photos[state.photoIndex].idView(), path);

    // Scoped so the destructor cuts the panel rail on every path out of here,
    // including the early return above and anything that throws later.
    Panel panel;
    if (!panel.begin()) {
        return;
    }
    const bool anyIcon = showLowBatteryIcon || showOfflineIcon;
    panel.displayFile(storage.fs(), path.data(), anyIcon ? overlayHook : nullptr);
}

Mode decideMode(WakeReason reason, MotionEvent event) {
    if (!Net::hasCredentials()) {
        return Mode::Provision;
    }

    if (reason == WakeReason::Motion) {
        if (event == MotionEvent::Shake) {
            return Mode::Sync;
        }
        // An unclassified interrupt is a spurious wake. Go straight back to
        // sleep without spending a 30 s refresh on it.
        return Mode::Normal;
    }

    if (rtcState().secondsSinceSync >= syncInterval(rtcState().syncFailures)) {
        return Mode::Sync;
    }
    return Mode::Normal;
}

// Debounce across deep sleep. esp_timer is restored from the RTC counter on
// wake, so it keeps counting while we're asleep and is the only monotonic clock
// this device has that survives a two-month nap.
//
// Hands bounce; without this one shake reads as four, each costing a sync.
bool motionTooSoon() {
    RtcState& state = rtcState();
    uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    if (state.bootCount > 1 && nowMs - state.lastMotionMillisSinceBoot < MOTION_DEBOUNCE_MS) {
        return true;
    }
    state.lastMotionMillisSinceBoot = nowMs;
    return false;
}

void runSync(bool triggeredByShake) {
    SyncResult result;

    {
        // Scoped so the radio is torn down before anything below spends 30 s
        // pushing pixels. Overlapping WiFi with a panel refresh would put the
        // two biggest current draws in the design on top of each other.
        Net net;
        if (net.connect()) {
            result = net.sync(storage);
        }
    }

    RtcState& state = rtcState();

    if (!result.ok) {
        // POWER: count the failure and back off. Retrying hourly through a
        // router outage costs 24 connect timeouts a day, which is more than the
        // entire rest of the budget. secondsSinceSync is reset either way so it
        // measures time since the last attempt, not since the last success.
        if (state.syncFailures < MAX_SYNC_FAILURES) {
            state.syncFailures++;
        }
        state.secondsSinceSync = 0;
        showOfflineIcon = state.syncFailures >= OFFLINE_ICON_AFTER_FAILURES;
        return;
    }

    state.syncFailures = 0;
    showOfflineIcon = false;

    storage.loadManifest(manifest);
    state.photoCount = manifest.size();
    state.secondsSinceSync = 0;

    if (!triggeredByShake) {
        return;
    }

    // The whole point of the gesture: land on the photo that was just
    // uploaded, not on the next one in rotation. The manifest carries
    // uploadedAt for every photo, so this needs no extra request.
    state.photoIndex = newestIndex(manifest);
}

}  // namespace

void setup() {
    Serial.begin(115200);

    if (!rtcStateValid()) {
        resetRtcState();
    }
    RtcState& state = rtcState();
    state.bootCount++;

    WakeReason reason = wakeReason();
    if (reason == WakeReason::Timer) {
        state.secondsSinceSync += REFRESH_INTERVAL_SECONDS;
    }

    if (!storage.begin()) {
        // Nothing to render and nothing to fix at runtime. Sleep rather than
        // spin — the panel keeps whatever it was already showing.
        sleepUntilNextEvent(REFRESH_INTERVAL_SECONDS);
    }
    storage.loadManifest(manifest);
    state.photoCount = manifest.size();

    const bool haveAccelerometer = motion.begin();
    (void)haveAccelerometer;
    MotionEvent event =
        reason == WakeReason::Motion ? motion.classifyWakeEvent() : MotionEvent::None;

    // Read before anything draws. The rail sags under a 45 mA refresh, so a
    // reading taken afterwards reports a battery several percent emptier than
    // it is and would trip the critical threshold early.
    battery = readBattery();
    showLowBatteryIcon = battery.low;
    state.lowBattery = battery.low ? 1 : 0;

    // From persisted state, not just from runSync: most wakes never sync, and
    // the icon still has to appear on those refreshes.
    showOfflineIcon = state.syncFailures >= OFFLINE_ICON_AFTER_FAILURES;

    // Recovered from a charge. Clearing the flag first means the normal path
    // below repaints a photo over the card without any special casing.
    if (state.emptyCardDrawn && battery.percent >= BATTERY_RECOVERY_PERCENT) {
        state.emptyCardDrawn = 0;
    }

    // POWER: below the critical threshold, stop showing photos and say why.
    //
    // E-ink holds its last image with no power at all, so the final refresh is
    // free forever — which makes it worth spending while there is still charge
    // to complete one. The alternative is a frozen photo that silently stops
    // changing, which reads as "the gift broke" rather than "plug it in".
    if (battery.critical) {
        if (!state.emptyCardDrawn) {
            Panel panel;
            if (panel.begin() && panel.displayGenerated(card::emptyBatteryCardRow)) {
                state.emptyCardDrawn = 1;
            }
        }
        motion.armForSleep();
        motion.powerDown();
        sleepUntilNextEvent(EMPTY_CHECK_INTERVAL_SECONDS);
    }

    Mode mode = decideMode(reason, event);

    if (reason == WakeReason::Motion && motionTooSoon()) {
        motion.armForSleep();
        motion.powerDown();
        sleepUntilNextEvent(REFRESH_INTERVAL_SECONDS);
    }

    switch (mode) {
        case Mode::Provision:
            // Blocks until the couple finishes or the portal times out.
            // Nothing on screen is worth preserving on a cold boot, so this is
            // the one path allowed to spend minutes awake.
            if (Net::runProvisioningPortal()) {
                state.provisioned = 1;
                runSync(false);
            }
            renderCurrentPhoto();
            break;

        case Mode::Sync:
            runSync(event == MotionEvent::Shake);
            renderCurrentPhoto();
            break;

        case Mode::Normal:
            if (reason != WakeReason::Motion) {
                state.photoIndex = nextIndex(state.photoIndex, state.photoCount);
                renderCurrentPhoto();
            }
            break;
    }

    motion.armForSleep();
    motion.powerDown();
    sleepUntilNextEvent(REFRESH_INTERVAL_SECONDS);
}

// POWER: intentionally empty and never reached. setup() always ends in
// sleepUntilNextEvent, which does not return. If you ever find yourself adding
// code here, something has gone wrong with the sleep path.
void loop() {}
