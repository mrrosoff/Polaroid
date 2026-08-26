#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "Config.h"

/*
 * Pure logic, no Arduino. This is the part of sync worth testing, so it
 * deliberately knows nothing about HTTP or the filesystem — see
 * test/test_native.
 */

namespace polaroid {

inline constexpr std::size_t ID_CAPACITY = 16;

using IdBuffer = std::array<char, ID_CAPACITY>;

inline constexpr std::string_view view(const IdBuffer& buffer) {
    const auto* end = std::find(buffer.begin(), buffer.end(), '\0');
    return {buffer.data(), static_cast<std::size_t>(end - buffer.begin())};
}

inline constexpr void assign(IdBuffer& buffer, std::string_view text) {
    buffer.fill('\0');
    const std::size_t length = std::min(text.size(), ID_CAPACITY - 1);
    std::copy_n(text.begin(), length, buffer.begin());
}

struct PhotoEntry {
    IdBuffer id{};
    IdBuffer hash{};
    std::uint32_t uploadedAt = 0;

    [[nodiscard]] constexpr std::string_view idView() const { return view(id); }
    [[nodiscard]] constexpr std::string_view hashView() const { return view(hash); }
};

[[nodiscard]] inline PhotoEntry makePhoto(std::string_view id, std::string_view hash,
                                          std::uint32_t uploadedAt) {
    PhotoEntry photo;
    assign(photo.id, id);
    assign(photo.hash, hash);
    photo.uploadedAt = uploadedAt;
    return photo;
}

struct Manifest {
    std::vector<PhotoEntry> photos;

    [[nodiscard]] const PhotoEntry* find(std::string_view id) const {
        const auto found = std::find_if(photos.begin(), photos.end(),
                                        [id](const PhotoEntry& p) { return p.idView() == id; });
        return found == photos.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool empty() const { return photos.empty(); }
    [[nodiscard]] std::uint16_t size() const { return static_cast<std::uint16_t>(photos.size()); }
};

/*
 * What a sync actually has to do. Everything not in fetch or remove is already
 * correct on disk and costs nothing.
 */
struct ManifestDiff {
    std::vector<PhotoEntry> fetch;
    std::vector<PhotoEntry> remove;
    std::uint16_t unchanged = 0;

    [[nodiscard]] bool empty() const { return fetch.empty() && remove.empty(); }
};

/*
 * A photo is fetched if it is new, or if its hash moved. The hash is over the
 * packed framebuffer rather than the source image, so re-tuning the dither
 * correctly invalidates everything and a no-op re-upload correctly doesn't.
 */
[[nodiscard]] inline ManifestDiff diffManifests(const Manifest& local, const Manifest& remote) {
    ManifestDiff diff;

    for (const PhotoEntry& want : remote.photos) {
        const PhotoEntry* have = local.find(want.idView());
        if (have == nullptr || have->hashView() != want.hashView()) {
            diff.fetch.push_back(want);
        } else {
            diff.unchanged++;
        }
    }

    std::copy_if(
        local.photos.begin(), local.photos.end(), std::back_inserter(diff.remove),
        [&remote](const PhotoEntry& have) { return remote.find(have.idView()) == nullptr; });

    return diff;
}

/*
 * NORMAL mode walks the manifest in upload order and wraps. Separated out so
 * the wrap-at-empty case has somewhere to be tested.
 */
/*
 * The manifest is newest-first, so index 0 is the newest photo and advancing
 * walks backwards in time. A shake, a deletion, or a cold boot returns to 0.
 *
 * That order is set by Net::fetchManifest keeping the response order, which
 * the native tests cannot reach -- they build no src/. Reversing it there
 * would break rotation silently, with nothing to catch it.
 */
[[nodiscard]] constexpr std::uint16_t nextIndex(std::uint16_t current, std::uint16_t count) {
    return count == 0 ? 0 : static_cast<std::uint16_t>((current + 1) % count);
}

/*
 * Seconds to wait before the next sync attempt. Zero failures is the normal
 * daily cadence; each consecutive failure doubles a one-hour retry until it
 * reaches that cadence again.
 */
[[nodiscard]] constexpr std::uint32_t syncInterval(std::uint8_t failures) {
    if (failures == 0) {
        return config::SYNC_INTERVAL_SECONDS;
    }
    const std::uint8_t shift =
        std::min<std::uint8_t>(static_cast<std::uint8_t>(failures - 1), config::MAX_SYNC_FAILURES);
    const std::uint32_t backoff = config::SYNC_RETRY_BASE_SECONDS << shift;
    return std::min(backoff, config::SYNC_INTERVAL_SECONDS);
}

}  // namespace polaroid
