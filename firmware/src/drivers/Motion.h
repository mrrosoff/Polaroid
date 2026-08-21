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

    // Reads INT1_SRC to see whether the activity detector fired. The register
    // latches, and reading it is what releases INT1 — so this has to happen on
    // every wake or the line stays asserted and we never sleep.
    MotionEvent classifyWakeEvent();

    void armForSleep();
    void powerDown();

  private:
    void configureActivityDetector();

    bool present_ = false;
};

}  // namespace polaroid
