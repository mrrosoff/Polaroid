#pragma once

#include <Arduino.h>

#include "Config.h"

namespace polaroid {

enum class MotionEvent : uint8_t {
    None,
    Shake,   // several direction reversals per second -> click detector
    Fridge,  // one directional swing -> activity threshold
};

class Motion {
  public:
    bool begin();

    // Reads CLICK_SRC and INT1_SRC to find out which detector fired. Both
    // registers latch, and reading them is what releases INT1 — so this has to
    // happen on every wake or the line stays asserted and we never sleep.
    MotionEvent classifyWakeEvent();

    void armForSleep();
    void powerDown();

  private:
    void configureClickDetector();
    void configureActivityDetector();

    bool present_ = false;
};

}  // namespace polaroid
