#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include <cstdint>
#include <span>
#include <string_view>

#include "Config.h"
#include "Manifest.h"

namespace polaroid {

using PhotoPath = std::array<char, 48>;

class Storage {
  public:
    [[nodiscard]] bool begin();

    bool loadManifest(Manifest& out);
    bool saveManifest(const Manifest& manifest);

    void photoPath(std::string_view id, std::span<char> out) const;
    [[nodiscard]] bool hasPhoto(std::string_view id) const;
    bool removePhoto(std::string_view id);

    [[nodiscard]] std::size_t freeBytes() const;
    [[nodiscard]] std::uint16_t capacityPhotos() const;

    fs::FS& fs() { return LittleFS; }

  private:
    bool mounted_ = false;
};

}  // namespace polaroid
