#pragma once

#include <Arduino.h>

#include "Config.h"

namespace polaroid {

/*
 * LIS3DH activity detector, wired to assert INT1 on a shake and wake the
 * device from deep sleep through ext0.
 */
class Motion {
  public:
    /*
     * Configures the part for low-power activity detection. False means the
     * LIS3DH did not answer on I2C, which is survivable: nothing asserts INT1,
     * so every wake looks like a timer wake.
     */
    bool begin();

    /*
     * Clears the latched INT1_SRC. Required on every wake, or the line stays
     * asserted and ext0 fires the instant we sleep.
     *
     * Deliberately reports nothing. begin() has already rewritten the
     * detector's registers by this point, clearing the source bits, so the
     * answer would always be "nothing fired". The esp_sleep wake cause is the
     * reliable signal, and INT1 has only one source.
     */
    void clearWakeLatch();

    /*
     * Clears the latch repeatedly until INT1 has stayed low long enough to mean
     * the movement is over. ext0 is level-triggered, so sleeping while the line
     * is still asserted wakes us straight back up.
     */
    void armForSleep();

    void powerDown();

  private:
    void configureActivityDetector();

    bool present_ = false;
};

}  // namespace polaroid
