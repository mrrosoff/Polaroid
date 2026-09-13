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

    /*
     * Deletes framebuffers the manifest does not mention. Sync only ever walks
     * manifest entries, so a file it has no record of is invisible to it and
     * never freed: a crash between writing a photo and saving the manifest
     * leaves one behind, as does dying mid-download. With three frames of
     * headroom that is the difference between replacement working and not.
     */
    std::uint16_t removeOrphans(const Manifest& manifest);

    /*
     * Whether the device has ever had a photo to show. Latches on and never
     * clears: an empty library then means "they were deleted", not "this is a
     * new frame", and the two want different screens.
     */
    [[nodiscard]] bool hasEverHeldPhoto() const;
    void markPhotoHeld();

    [[nodiscard]] std::size_t freeBytes() const;
    [[nodiscard]] std::uint16_t capacityPhotos() const;

    fs::FS& fs() { return LittleFS; }

  private:
    bool mounted_ = false;
};

}  // namespace polaroid
