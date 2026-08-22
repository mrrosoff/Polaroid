#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdarg>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "drivers/Battery.h"
#include "Config.h"
#include "Manifest.h"
#include "drivers/Motion.h"
#include "net/Net.h"
#include "Overlay.h"
#include "drivers/Panel.h"
#include "system/Log.h"
#include "system/State.h"
#include "StatusCard.h"
#include "drivers/Storage.h"

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

// A wake either syncs or it does not. Shakes sync because that is the gesture;
// a cold boot syncs because it has nothing to show and a zeroed clock; the
// timer syncs once its interval is up.
[[nodiscard]] bool shouldSync(WakeReason reason, MotionEvent event) {
    if (reason == WakeReason::Motion) {
        return event == MotionEvent::Shake;
    }
    if (reason == WakeReason::ColdBoot) {
        return true;
    }
    return rtcState().secondsSinceSync >= syncInterval(rtcState().syncFailures);
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
        // Scoped so the radio is torn down before anything below spends 20 s
        // pushing pixels. Overlapping WiFi with a panel refresh would put the
        // two biggest current draws in the design on top of each other.
        Net net;
        if (net.connect()) {
            result = net.sync(storage);
        }
    }

    RtcState& state = rtcState();

    if (!result.ok) {
        logf("sync failed (%u in a row)", state.syncFailures + 1);
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
    logf("sync ok: %u fetched, %u removed", result.fetched, result.removed);

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

// Every exit from setup() runs this. Clearing the latches is not optional:
// ext0 is level-triggered, so an asserted INT1 wakes us the instant we sleep.
[[noreturn]] void powerDownAndSleep(uint32_t seconds) {
    motion.armForSleep();
    motion.powerDown();

#ifdef POLAROID_NO_SLEEP
    // Dev build: never deep sleep. Sleep drops USB, which takes away both the
    // serial log and the ability to flash without holding BOOT through a
    // reset. Idling here keeps the bus up indefinitely; a shake restarts the
    // device, which runs the whole cycle again and is as close to a real wake
    // as this build gets. Compiled out of polaroid-xiao, where staying awake
    // would empty the battery in a day.
    logf("dev build: staying awake. shake to run another cycle.");
    for (std::uint32_t tick = 0;; ++tick) {
        if (digitalRead(PIN_ACCEL_INT1) == HIGH) {
            logf("motion - restarting");
            delay(200);
            esp_restart();
        }
        if (tick % 300 == 299) {
            logf("idle");
        }
        delay(100);
    }
#endif

    sleepUntilNextEvent(seconds);
}

}  // namespace

void setup() {
    Serial.begin(115200);

#ifdef POLAROID_BRINGUP
    // USB CDC takes about a second to enumerate and for the host to raise DTR,
    // and setup() is otherwise finished before that happens — so every logf()
    // below is suppressed and a bring-up run looks silent. Only in the bringup
    // build: on battery there is no host and this would be a second of wasted
    // wake on every single refresh.
    for (std::uint32_t waited = 0; !Serial && waited < 2000; waited += 50) {
        delay(50);
    }
    delay(200);
#endif

    if (!rtcStateValid()) {
        resetRtcState();
    }
    RtcState& state = rtcState();
    state.bootCount++;

    WakeReason reason = wakeReason();
    logf("boot %lu, wake=%s", static_cast<unsigned long>(state.bootCount),
         reason == WakeReason::Motion  ? "motion"
         : reason == WakeReason::Timer ? "timer"
                                       : "cold");
    if (reason == WakeReason::Timer) {
        state.secondsSinceSync += REFRESH_INTERVAL_SECONDS;
    }

    if (!storage.begin()) {
        logf("FATAL: no filesystem; sleeping");
        // Nothing to render and nothing to fix at runtime. Sleep rather than
        // spin — the panel keeps whatever it was already showing.
        sleepUntilNextEvent(REFRESH_INTERVAL_SECONDS);
    }
    storage.loadManifest(manifest);
    state.photoCount = manifest.size();
    logf("filesystem mounted, %u photos on flash", state.photoCount);

    // A missing accelerometer is survivable: the timer still rotates photos and
    // classifyWakeEvent reports None, so every wake looks like a timer wake.
    motion.begin();
    const MotionEvent event =
        reason == WakeReason::Motion ? motion.classifyWakeEvent() : MotionEvent::None;

    // Read before anything draws. The rail sags under a 45 mA refresh, so a
    // reading taken afterwards reports a battery several percent emptier than
    // it is and would trip the critical threshold early.
    battery = readBattery();
    logf("battery %.2f V, %u%%%s", battery.volts, battery.percent,
         battery.critical ? " CRITICAL" : battery.low ? " low" : "");
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
        powerDownAndSleep(EMPTY_CHECK_INTERVAL_SECONDS);
    }

    if (reason == WakeReason::Motion && motionTooSoon()) {
        powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
    }

    const bool syncing = shouldSync(reason, event);
    logf("%s", syncing ? "syncing" : "advancing");

    if (syncing) {
        runSync(event == MotionEvent::Shake);
    } else if (reason == WakeReason::Motion) {
        // A motion wake that is not a shake is spurious. Back to sleep without
        // spending a refresh on it.
        powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
    } else {
        state.photoIndex = nextIndex(state.photoIndex, state.photoCount);
    }

    renderCurrentPhoto();

    powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
}

// POWER: intentionally empty and never reached. setup() always ends in
// sleepUntilNextEvent, which does not return. If you ever find yourself adding
// code here, something has gone wrong with the sleep path.
void loop() {}
