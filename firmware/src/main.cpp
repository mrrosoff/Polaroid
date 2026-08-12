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
#include "Storage.h"

using namespace config;
using namespace polaroid;

namespace {

Storage storage;
Motion motion;
Manifest manifest;
BatteryReading battery;

bool showLowBatteryIcon = false;

bool overlayHook(std::uint16_t row, std::span<std::uint8_t> rowBytes) {
    return lowBatteryOverlay(row, rowBytes);
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
    storage.photoPath(manifest.photos[state.photoIndex].id, path);

    // Scoped so the destructor cuts the panel rail on every path out of here,
    // including the early return above and anything that throws later.
    Panel panel;
    if (!panel.begin()) {
        return;
    }
    panel.displayFile(storage.fs(), path.data(), showLowBatteryIcon ? overlayHook : nullptr);
}

void jumpToPhoto(std::string_view id) {
    const auto found = std::find_if(
        manifest.photos.begin(), manifest.photos.end(),
        [id](const PhotoEntry& photo) { return id == std::string_view(photo.id); });

    if (found != manifest.photos.end()) {
        rtcState().photoIndex =
            static_cast<std::uint16_t>(std::distance(manifest.photos.begin(), found));
    }
}

Mode decideMode(WakeReason reason, MotionEvent event) {
    if (!Net::hasCredentials()) {
        return Mode::Provision;
    }

    if (reason == WakeReason::Motion) {
        if (event == MotionEvent::Shake) {
            return Mode::Sync;
        }
        if (event == MotionEvent::Fridge && FRIDGE_MODE_ENABLED) {
            return Mode::Fridge;
        }
        // An unclassified interrupt is a spurious wake. Go straight back to
        // sleep without spending a 30 s refresh on it.
        return Mode::Normal;
    }

    if (rtcState().secondsSinceSync >= SYNC_INTERVAL_SECONDS) {
        return Mode::Sync;
    }
    return Mode::Normal;
}

// Debounce across deep sleep. esp_timer is restored from the RTC counter on
// wake, so it keeps counting while we're asleep and is the only monotonic clock
// this device has that survives a two-month nap.
//
// Fridge doors bounce and so do hands; without this one shake reads as four,
// each costing a 30 s refresh.
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
        if (!net.connect()) {
            // A failed sync is not an error state. Fall through and render
            // whatever is already on the device.
            return;
        }
        result = net.sync(storage, triggeredByShake);
    }

    if (!result.ok) {
        return;
    }

    storage.loadManifest(manifest);

    RtcState& state = rtcState();
    state.photoCount = manifest.size();
    state.secondsSinceSync = 0;

    if (!triggeredByShake) {
        return;
    }

    // The whole point of the gesture: land on the photo that was just
    // uploaded, not on the next one in rotation. Fall back to the newest
    // local entry if /recent didn't answer.
    if (result.newestId.has_value()) {
        jumpToPhoto(*result.newestId);
    } else {
        state.photoIndex = newestIndex(manifest);
    }
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

    battery = readBattery();
    showLowBatteryIcon = battery.low;
    state.lowBattery = battery.low ? 1 : 0;

    // POWER: below the critical threshold we stop refreshing entirely. E-ink
    // holds its last image with no power at all, so the couple is left looking
    // at a photo rather than at whatever half-drawn frame you get when the rail
    // sags 20 seconds into a refresh.
    if (battery.critical) {
        motion.armForSleep();
        motion.powerDown();
        sleepUntilNextEvent(REFRESH_INTERVAL_SECONDS * 4);
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

        case Mode::Fridge:
            state.photoIndex = nextIndex(state.photoIndex, state.photoCount);
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
