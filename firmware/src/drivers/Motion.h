#pragma once

#include <Arduino.h>

#include "Config.h"

namespace polaroid {

enum class MotionEvent : uint8_t {
    None,
    Shake,
};

class Motion {
  public:
    bool begin();

    // Clears the latched INT1_SRC. Must happen on every wake or the line stays
    // asserted and we never sleep. It does not report what fired: begin() has
    // already rewritten the detector's registers by this point, which clears
    // the source bits, so the answer would always be "nothing". The wake cause
    // from esp_sleep is the reliable signal, and INT1 has only one source.
    void clearWakeLatch();

    void armForSleep();
    void powerDown();

  private:
    void configureActivityDetector();

    bool present_ = false;
};

}  // namespace polaroid
