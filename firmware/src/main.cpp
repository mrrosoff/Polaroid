#include <Arduino.h>
#include <Wire.h>
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
// Why the previous wake ended, readable on the next boot.
enum : std::uint8_t {
    EXIT_NORMAL = 1,
    EXIT_NO_FILESYSTEM = 2,
    EXIT_CRITICAL_BATTERY = 3,
    EXIT_MOTION_TOO_SOON = 4,
    EXIT_SPURIOUS_MOTION = 5,
};

// RTC_DATA_ATTR survives deep sleep but not a reset through EN, so anything
// recorded for the next boot is destroyed by pressing the button to go and
// read it. On the bench the device has to wake itself instead, and an hour is
// too long to wait for that.
[[nodiscard]] std::uint32_t sleepSeconds(std::uint32_t seconds) {
#ifdef POLAROID_BRINGUP
    return seconds > 45 ? 45 : seconds;
#else
    return seconds;
#endif
}

[[noreturn]] void powerDownAndSleep(uint32_t seconds) {
#ifdef POLAROID_BRINGUP
    // Is the accelerometer still alive now that the panel rail is cut? If the
    // LIS3DH is fed from the gated rail rather than from 3V3, it is dead by
    // this point and cannot assert INT1, which would look exactly like a
    // broken wake. WHO_AM_I answers that in one transaction.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeOut(20);
    Wire.beginTransmission(ACCEL_I2C_ADDRESS);
    Wire.write(0x0F);
    const bool addressed = Wire.endTransmission(false) == 0;
    Wire.requestFrom(static_cast<std::uint8_t>(ACCEL_I2C_ADDRESS), static_cast<std::uint8_t>(1));
    const int who = Wire.available() ? Wire.read() : -1;
    // Everything logged at the top of setup() is lost: USB takes longer to
    // enumerate after a deep-sleep wake than the host wait allows. This line
    // runs seconds later and does get through, so the wake's verdict is
    // reported here rather than on the next boot.
    logf("wake=%s exit=%u motionWakes=%u | LIS3DH ack=%d WHO_AM_I=0x%02X INT1=%d",
         wakeReason() == WakeReason::Motion  ? "motion"
         : wakeReason() == WakeReason::Timer ? "timer"
                                             : "cold",
         rtcState().lastExit, rtcState().motionWakes, addressed ? 1 : 0, who,
         digitalRead(PIN_ACCEL_INT1));
#endif

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

    sleepUntilNextEvent(sleepSeconds(seconds));
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
    logf("prev exit=%u, motion wakes=%u", state.lastExit, state.motionWakes);
    if (wakeReason() == WakeReason::Motion) {
        state.motionWakes++;
    }
    logf("boot %lu, wake=%s", static_cast<unsigned long>(state.bootCount),
         reason == WakeReason::Motion  ? "motion"
         : reason == WakeReason::Timer ? "timer"
                                       : "cold");
    if (reason == WakeReason::Timer) {
        state.secondsSinceSync += REFRESH_INTERVAL_SECONDS;
    }

    if (!storage.begin()) {
        logf("FATAL: no filesystem; sleeping");
        state.lastExit = EXIT_NO_FILESYSTEM;
        // Nothing to render and nothing to fix at runtime. Sleep rather than
        // spin — the panel keeps whatever it was already showing.
        sleepUntilNextEvent(REFRESH_INTERVAL_SECONDS);
    }
    storage.loadManifest(manifest);
    state.photoCount = manifest.size();
    logf("filesystem mounted, %u photos on flash", state.photoCount);

    // A missing accelerometer is survivable: nothing asserts INT1, so the timer
    // still rotates photos and every wake looks like a timer wake.
    motion.begin();
    // INT1 has exactly one source, so a motion wake is a shake. Asking the
    // LIS3DH which detector fired would always answer "none": motion.begin()
    // above has already rewritten its registers, and that clears the latched
    // source bits before we could read them.
    const MotionEvent event =
        reason == WakeReason::Motion ? MotionEvent::Shake : MotionEvent::None;
    if (reason == WakeReason::Motion) {
        motion.clearWakeLatch();
    }

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
        state.lastExit = EXIT_CRITICAL_BATTERY;
        powerDownAndSleep(EMPTY_CHECK_INTERVAL_SECONDS);
    }

    if (reason == WakeReason::Motion && motionTooSoon()) {
        state.lastExit = EXIT_MOTION_TOO_SOON;
        powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
    }

    const bool syncing = shouldSync(reason, event);
    logf("%s", syncing ? "syncing" : "advancing");

    if (syncing) {
        runSync(event == MotionEvent::Shake);
    } else if (reason == WakeReason::Motion) {
        // A motion wake that is not a shake is spurious. Back to sleep without
        // spending a refresh on it.
        state.lastExit = EXIT_SPURIOUS_MOTION;
        powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
    } else {
        state.photoIndex = nextIndex(state.photoIndex, state.photoCount);
    }

    renderCurrentPhoto();

    state.lastExit = EXIT_NORMAL;
    powerDownAndSleep(REFRESH_INTERVAL_SECONDS);
}

// POWER: intentionally empty and never reached. setup() always ends in
// sleepUntilNextEvent, which does not return. If you ever find yourself adding
// code here, something has gone wrong with the sleep path.
void loop() {}
