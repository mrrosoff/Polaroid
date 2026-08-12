#pragma once

#include <Arduino.h>

#include <cstdint>
#include <optional>
#include <string>

#include "Config.h"
#include "Manifest.h"
#include "Storage.h"

namespace polaroid {

struct SyncResult {
    bool ok = false;
    std::uint16_t fetched = 0;
    std::uint16_t removed = 0;
    std::optional<std::string> newestId;
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

    // True if credentials are stored. Cheap — reads NVS, no radio.
    [[nodiscard]] static bool hasCredentials();

    // Captive portal. Blocks until the couple finishes or PROVISION_TIMEOUT_MS.
    [[nodiscard]] static bool runProvisioningPortal();

    [[nodiscard]] bool connect();
    void disconnect();

    [[nodiscard]] bool fetchManifest(Manifest& out);
    [[nodiscard]] bool downloadPhoto(Storage& storage, const PhotoEntry& photo);
    [[nodiscard]] std::optional<std::string> fetchNewestId();

    // The whole of SYNC mode: manifest, diff, fetch, delete, save. Every
    // failure path leaves local storage exactly as it was.
    SyncResult sync(Storage& storage, bool wantNewest);

  private:
    bool connected_ = false;
};

}  // namespace polaroid
