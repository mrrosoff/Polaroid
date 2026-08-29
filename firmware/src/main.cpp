#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_private/esp_clk.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdarg>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "Config.h"
#include "Manifest.h"
#include "Overlay.h"
#include "StatusCard.h"
#include "drivers/Battery.h"
#include "drivers/Motion.h"
#include "drivers/Panel.h"
#include "drivers/Storage.h"
#include "net/Net.h"
#include "system/Log.h"
#include "system/State.h"

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

/*
 * Renders whatever is at rtcState().photoIndex. Every failure here is
 * non-fatal: e-ink holds its last image, so the worst case is that the couple
 * looks at yesterday's photo for another hour.
 */
void renderCurrentPhoto() {
    RtcState& state = rtcState();

    /*
     * An empty library has to say so. E-ink holds its last image, so returning
     * here would leave whatever was drawn last sitting on the panel, and a
     * frame with nothing in it would look exactly like a frame that had
     * stopped working.
     *
     * Drawn once. The state persists until a sync brings a photo back, and
     * repainting the identical card on every wake would spend the whole daily
     * budget on it.
     */
    if (manifest.photos.empty()) {
        /*
         * A frame that has never held a photo shows nothing at all. It is a
         * gift, and the first thing anyone sees should be clean paper rather
         * than an instruction. Once it has held one, empty means they were
         * deleted, and that is worth saying.
         */
        const std::uint8_t want = storage.hasEverHeldPhoto() ? PANEL_NO_PHOTOS_CARD : PANEL_BLANK;
        if (state.panelShows == want) {
            return;
        }
        Panel panel;
        if (!panel.begin()) {
            return;
        }
        const bool drawn = want == PANEL_BLANK ? panel.displaySolid(config::INK_WHITE)
                                               : panel.displayGenerated(card::noPhotosCardRow);
        if (drawn) {
            state.panelShows = want;
        }
        return;
    }
    storage.markPhotoHeld();

    if (state.photoIndex >= manifest.photos.size()) {
        state.photoIndex = 0;
    }

    std::array<char, 48> path{};
    storage.photoPath(manifest.photos[state.photoIndex].idView(), path);

    /*
     * Scoped so the destructor cuts the panel rail on every path out of here,
     * including the early return above and anything that throws later.
     */
    Panel panel;
    if (!panel.begin()) {
        return;
    }
    const bool anyIcon = showLowBatteryIcon || showOfflineIcon;
    if (panel.displayFile(storage.fs(), path.data(), anyIcon ? overlayHook : nullptr)) {
        state.panelShows = PANEL_PHOTO;
    }
}

/*
 * A wake either syncs or it does not. Motion always syncs -- INT1 has one
 * source and there is no second gesture to tell apart -- a cold boot syncs
 * because it has nothing to show and a zeroed clock, and the timer syncs once
 * its interval is up.
 */
[[nodiscard]] bool shouldSync(WakeReason reason) {
    if (reason == WakeReason::Motion || reason == WakeReason::ColdBoot) {
        return true;
    }
    return rtcState().secondsSinceSync >= syncInterval(rtcState().syncFailures);
}

/*
 * Debounce across deep sleep: hands bounce, and one shake otherwise reads as
 * four, each costing a sync.
 *
 * The clock must be the RTC's. esp_timer_get_time() counts from
 * esp_timer_init(), and a deep-sleep wake is an application startup, so it
 * restarts near zero every wake -- against which every shake looked like a
 * bounce and was discarded.
 *
 * esp_clk_rtc_time() lives behind esp_private/, so a framework bump can move
 * it. If it does, the replacement has to keep counting through deep sleep;
 * anything measuring uptime brings the bug straight back.
 */
bool motionTooSoon() {
    RtcState& state = rtcState();
    const std::uint64_t nowMs = esp_clk_rtc_time() / 1000ULL;

    /*
     * Zero is "no motion wake recorded yet" and must not read as "just now".
     * The old guard used bootCount > 1 for that, which does not cover the
     * first motion wake after a cold boot, where the stored value is still
     * zero.
     */
    if (state.lastMotionMs != 0 && nowMs - state.lastMotionMs < MOTION_DEBOUNCE_MS) {
        return true;
    }
    state.lastMotionMs = nowMs;
    return false;
}

SyncResult runSync() {
    SyncResult result;

    {
        /*
         * Scoped so the radio is torn down before anything below spends 20 s
         * pushing pixels. Overlapping WiFi with a panel refresh would put the
         * two biggest current draws in the design on top of each other.
         */
        Net net;
        if (net.connect()) {
            result = net.sync(storage);
        }
    }

    RtcState& state = rtcState();

    if (!result.ok) {
        logf("sync failed (%u in a row)", state.syncFailures + 1);
        /*
         * POWER: count the failure and back off. Retrying hourly through a
         * router outage costs 24 connect timeouts a day, which is more than the
         * entire rest of the budget. secondsSinceSync is reset either way so it
         * measures time since the last attempt, not since the last success.
         */
        if (state.syncFailures < MAX_SYNC_FAILURES) {
            state.syncFailures++;
        }
        state.secondsSinceSync = 0;
        showOfflineIcon = state.syncFailures >= OFFLINE_ICON_AFTER_FAILURES;
        return result;
    }

    state.syncFailures = 0;
    showOfflineIcon = false;
    logf("sync ok: %u fetched, %u removed", result.fetched, result.removed);

    storage.loadManifest(manifest);
    state.photoCount = manifest.size();
    state.secondsSinceSync = 0;
    return result;
}

/*
 * Every exit from setup() runs this. Clearing the latches is not optional:
 * ext0 is level-triggered, so an asserted INT1 wakes us the instant we sleep.
 * Why the previous wake ended, readable on the next boot.
 */
enum : std::uint8_t {
    EXIT_NORMAL = 1,
    EXIT_NO_FILESYSTEM = 2,
    EXIT_CRITICAL_BATTERY = 3,
    EXIT_MOTION_TOO_SOON = 4,
};

[[noreturn]] void powerDownAndSleep(uint32_t seconds) {
    /*
     * The battery is read at the top of setup() and logged there too, but that
     * line is written about 0.6 s into the wake and USB takes roughly a second
     * to enumerate -- so with a host attached it is always dropped before
     * anyone can see it. By here the wake has been up for the length of a
     * refresh and the port is reliably open.
     *
     * POWER: costs nothing on battery. logf returns before touching the port
     * when no host is connected, which on a frame on a fridge is always.
     */
    logf("sleeping %lus | battery %.2f V, %u%% | wake=%s exit=%u",
         static_cast<unsigned long>(seconds), battery.volts, battery.percent,
         wakeReason() == WakeReason::Motion  ? "motion"
         : wakeReason() == WakeReason::Timer ? "timer"
                                             : "cold",
         rtcState().lastExit);

    motion.armForSleep();
    motion.powerDown();

    sleepUntilNextEvent(seconds);
}

}  // namespace

void setup() {
    Serial.begin(115200);

    /*
     * Release the pads held through deep sleep. Without this PIN_EPD_PWR stays
     * latched low and the panel never powers up again: the frame would go dark
     * permanently on the second wake.
     */
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_EPD_PWR));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_STATUS_LED));

    if (!rtcStateValid()) {
        resetRtcState();
    }
    RtcState& state = rtcState();
    state.bootCount++;

    WakeReason reason = wakeReason();
    logf("prev exit=%u", state.lastExit);
    logf("boot %lu, wake=%s", static_cast<unsigned long>(state.bootCount),
         reason == WakeReason::Motion  ? "motion"
         : reason == WakeReason::Timer ? "timer"
                                       : "cold");
    if (reason == WakeReason::Timer) {
        state.secondsSinceSync += REFRESH_INTERVAL_SECONDS;
    }

    /*
     * A missing accelerometer is survivable: nothing asserts INT1, so the timer
     * still rotates photos and every wake looks like a timer wake.
     */
    motion.begin();
    /*
     * INT1 has exactly one source, so a motion wake IS a shake. Asking the
     * LIS3DH which detector fired would always answer "none": motion.begin()
     * above has already rewritten its registers, and that clears the latched
     * source bits before we could read them.
     */
    if (reason == WakeReason::Motion) {
        motion.clearWakeLatch();
    }

    if (!storage.begin()) {
        logf("FATAL: no filesystem; sleeping");
        state.lastExit = EXIT_NO_FILESYSTEM;
        /*
         * Nothing to render and nothing to fix at runtime. Sleep rather than
         * spin -- the panel keeps whatever it was already showing. Through
         * powerDownAndSleep like every other exit, so the INT1 latch is
         * cleared: leaving by sleepUntilNextEvent() directly would arm ext0
         * against a line still asserted by the shake that woke us, and the
         * device would spin awake until the battery was flat.
         */
        powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
    }
    storage.loadManifest(manifest);
    state.photoCount = manifest.size();
    logf("filesystem mounted, %u photos on flash", state.photoCount);

    /*
     * Read before anything draws. The rail sags under a 45 mA refresh, so a
     * reading taken afterwards reports a battery several percent emptier than
     * it is and would trip the critical threshold early.
     */
    battery = readBattery();

#ifdef POLAROID_SLEEP_ONLY
    /*
     * DIAGNOSTIC: stop here. Everything above this line is the accelerometer
     * (6 uA by datasheet, plus the breakout's 130 uA LED) and one ADC read.
     * Nothing below it runs: no filesystem, no radio, no panel rail, no
     * refresh.
     *
     * So if the sleep current is still milliamps in this build, it is not
     * anything this firmware drives -- it is the XIAO, its regulator, the
     * charge IC, or the wiring, and no code change will reach it. If it falls
     * to microamps, the load is one of the things skipped here and it can be
     * added back one at a time.
     *
     * The motion wake is deliberately left armed so the result can be read by
     * shaking the device rather than waiting out an hour-long timer.
     */
    /*
     * Wait for a USB host before sleeping, or this build is unreadable: it
     * sleeps a few hundred ms after boot, and enumeration takes about a
     * second, so the battery line would always be written into a void.
     *
     * Costs about 4 s of wake per hour on battery, where no host ever answers.
     * At ~40 mA that is near 1 mAh/day -- a tenth of the design budget, but
     * under half a percent of the ~240 mAh/day this build exists to measure,
     * so it cannot mask the thing it is looking for.
     */
    for (int waited = 0; waited < 80 && !Serial; waited++) {
        delay(50);
    }
    delay(200);

    state.lastExit = EXIT_NORMAL;
    powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
#endif
    logf("battery %.2f V, %u%%%s", battery.volts, battery.percent,
         battery.critical ? " CRITICAL"
         : battery.low    ? " low"
                          : "");
    showLowBatteryIcon = battery.low;

    /*
     * From persisted state, not just from runSync: most wakes never sync, and
     * the icon still has to appear on those refreshes.
     */
    showOfflineIcon = state.syncFailures >= OFFLINE_ICON_AFTER_FAILURES;

    /*
     * POWER: below the critical threshold, stop showing photos and say why.
     *
     * E-ink holds its last image with no power at all, so the final refresh is
     * free forever -- which makes it worth spending while there is still
     * charge to complete one. The alternative is a frozen photo that silently
     * stops changing, which reads as "the gift broke" rather than "plug it in".
     *
     * Hysteresis: enter at CRITICAL_BATTERY_PERCENT, leave at
     * BATTERY_RECOVERY_PERCENT. Without the gap a cell sitting on the
     * threshold alternates between a photo and the card every hour, and each
     * swap costs a full refresh out of a battery that has none to spare.
     *
     * This takes precedence over the empty-library card below: it returns
     * without ever reaching the render, so a device that is both flat and
     * empty says CHARGE ME, which is the one of the two that is actionable.
     */
    const bool batteryCardWins = state.panelShows == PANEL_BATTERY_CARD
                                     ? battery.percent < BATTERY_RECOVERY_PERCENT
                                     : battery.critical;

    if (batteryCardWins) {
        if (state.panelShows != PANEL_BATTERY_CARD) {
            Panel panel;
            if (panel.begin() && panel.displayGenerated(card::emptyBatteryCardRow)) {
                state.panelShows = PANEL_BATTERY_CARD;
            }
        }
        state.lastExit = EXIT_CRITICAL_BATTERY;
        powerDownAndSleep(EMPTY_CHECK_INTERVAL_SECONDS);
    }

    if (reason == WakeReason::Motion && motionTooSoon()) {
        state.lastExit = EXIT_MOTION_TOO_SOON;
        powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
    }

    const bool syncing = shouldSync(reason);
    logf("%s", syncing ? "syncing" : "advancing");

    /*
     * Index 0 is the newest photo, so "back to zero" and "show what just
     * arrived" are the same instruction. A shake and a cold boot want it, and
     * so does any sync that changed the list: fetching or deleting shifts
     * every later index, so keeping the old number would point at a different
     * photo, and advancing by one after fetching exactly one lands back on the
     * image already on screen.
     *
     * Everything else advances by one, walking backwards in time.
     */
    bool toNewest = reason != WakeReason::Timer;

    if (syncing) {
        const SyncResult result = runSync();
        toNewest = toNewest || result.removed > 0 || result.fetched > 0;
    }

    state.photoIndex = toNewest ? 0 : nextIndex(state.photoIndex, state.photoCount);

    renderCurrentPhoto();

    state.lastExit = EXIT_NORMAL;
    powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
}

/*
 * POWER: intentionally empty and never reached. setup() always ends in
 * sleepUntilNextEvent, which does not return. If you ever find yourself adding
 * code here, something has gone wrong with the sleep path.
 */
void loop() {}
