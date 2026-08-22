#pragma once

#include <Arduino.h>

#include <cstdint>

#include "Config.h"
#include "Manifest.h"
#include "drivers/Storage.h"

namespace polaroid {

struct SyncResult {
    bool ok = false;
    std::uint16_t fetched = 0;
    std::uint16_t removed = 0;
};

// RAII: the destructor tears the radio down. Every early return out of a sync
// is a path where forgetting to do that costs the battery, so it is tied to
// the scope rather than to remembering.
class Net {
  public:
    Net() = default;
    ~Net();

    Net(const Net&) = delete;
    Net& operator=(const Net&) = delete;

    [[nodiscard]] bool connect();
    void disconnect();

    [[nodiscard]] bool fetchManifest(Manifest& out);
    [[nodiscard]] bool downloadPhoto(Storage& storage, const PhotoEntry& photo);

    // Every failure path leaves local storage exactly as it was.
    SyncResult sync(Storage& storage);

  private:
    bool connected_ = false;
};

}  // namespace polaroid
