#include "Storage.h"

#include <ArduinoJson.h>

using namespace config;

namespace polaroid {

bool Storage::begin() {
    mounted_ = LittleFS.begin(true);
    if (mounted_ && !LittleFS.exists(PHOTO_DIR)) {
        LittleFS.mkdir(PHOTO_DIR);
    }
    return mounted_;
}

void Storage::photoPath(std::string_view id, std::span<char> out) const {
    snprintf(out.data(), out.size(), "%s/%.*s.bin", PHOTO_DIR, static_cast<int>(id.size()),
             id.data());
}

bool Storage::hasPhoto(std::string_view id) const {
    PhotoPath path{};
    photoPath(id, path);

    File file = LittleFS.open(path.data(), FILE_READ);
    if (!file) {
        return false;
    }
    // Size is the completeness check. A short file is an interrupted download.
    const bool complete = file.size() == PANEL_BYTES;
    file.close();
    return complete;
}

bool Storage::removePhoto(std::string_view id) {
    PhotoPath path{};
    photoPath(id, path);
    return LittleFS.remove(path.data());
}

std::size_t Storage::freeBytes() const { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

std::uint16_t Storage::capacityPhotos() const {
    return static_cast<std::uint16_t>(LittleFS.totalBytes() / PANEL_BYTES);
}

bool Storage::loadManifest(Manifest& out) {
    out.photos.clear();

    File file = LittleFS.open(MANIFEST_PATH, FILE_READ);
    if (!file) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        return false;
    }

    for (JsonObject entry : doc["photos"].as<JsonArray>()) {
        PhotoEntry photo = makePhoto(entry["id"] | "", entry["hash"] | "",
                                     entry["uploadedAt"] | 0u);
        if (!photo.idView().empty()) {
            out.photos.push_back(photo);
        }
    }
    return true;
}

bool Storage::saveManifest(const Manifest& manifest) {
    JsonDocument doc;
    doc["version"] = 1;
    JsonArray photos = doc["photos"].to<JsonArray>();
    for (const PhotoEntry& photo : manifest.photos) {
        JsonObject entry = photos.add<JsonObject>();
        entry["id"] = photo.id.data();
        entry["hash"] = photo.hash.data();
        entry["uploadedAt"] = photo.uploadedAt;
    }

    // Write-then-rename: a brownout mid-write must not leave a manifest that
    // parses but disagrees with what's on disk.
    const char* temp = "/manifest.tmp";
    File file = LittleFS.open(temp, FILE_WRITE);
    if (!file) {
        return false;
    }
    bool ok = serializeJson(doc, file) > 0;
    file.close();

    if (!ok) {
        LittleFS.remove(temp);
        return false;
    }
    LittleFS.remove(MANIFEST_PATH);
    return LittleFS.rename(temp, MANIFEST_PATH);
}

}  // namespace polaroid
