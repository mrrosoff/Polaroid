#include "Net.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

#include <algorithm>

#include "Secrets.h"

using namespace config;

namespace polaroid {

namespace {

// POWER: certificate validation costs a few hundred ms of CPU at ~40 mA and
// needs a trusted clock, which this device does not have after a two-month
// sleep. The device token is the real security boundary and the payload is
// wedding photos, so the connection is encrypted but not pinned.
void configureClient(WiFiClientSecure& client) {
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);
}

[[nodiscard]] bool beginRequest(HTTPClient& http, WiFiClientSecure& client, const String& url) {
    if (!http.begin(client, url)) {
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + POLAROID_DEVICE_TOKEN);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    return true;
}

}  // namespace

Net::~Net() {
    if (connected_) {
        disconnect();
    }
}

bool Net::hasCredentials() {
    wifi_config_t stored{};
    if (esp_wifi_get_config(WIFI_IF_STA, &stored) != ESP_OK) {
        return false;
    }
    return stored.sta.ssid[0] != '\0';
}

bool Net::runProvisioningPortal() {
    WiFiManager manager;
    // POWER: the timeout is the point. Without it an abandoned setup sits at
    // ~80 mA in AP mode until the battery is flat.
    manager.setConfigPortalTimeout(PROVISION_TIMEOUT_MS / 1000);
    manager.setTitle("Polaroid");
    manager.setDarkMode(false);

    return manager.autoConnect(PROVISION_AP_NAME);
}

bool Net::connect() {
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.begin();

    const std::uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return false;
        }
        delay(100);
    }

    connected_ = true;
    return true;
}

void Net::disconnect() {
    // Credentials are kept: false on the second argument. Only sleepUntilNext
    // wipes them, and only because it also deinits the driver.
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    connected_ = false;
}

bool Net::fetchManifest(Manifest& out) {
    if (!connected_) {
        return false;
    }

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    if (!beginRequest(http, client, String(API_BASE_URL) + "/manifest")) {
        return false;
    }

    if (http.GET() != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();
    if (error) {
        return false;
    }

    out.photos.clear();
    for (JsonObject entry : doc["photos"].as<JsonArray>()) {
        if (out.photos.size() >= MAX_PHOTOS) {
            break;
        }
        PhotoEntry photo = makePhoto(entry["id"] | "", entry["hash"] | "",
                                     entry["uploadedAt"] | 0u);
        if (!photo.idView().empty()) {
            out.photos.push_back(photo);
        }
    }
    return true;
}

bool Net::downloadPhoto(Storage& storage, const PhotoEntry& photo) {
    if (!connected_) {
        return false;
    }

    PhotoPath finalPath{};
    storage.photoPath(photo.idView(), finalPath);

    // Download to a temp name and rename on success. A half-written photo must
    // never be reachable through the manifest — the panel would render the top
    // half of a wedding and the bottom half of nothing.
    constexpr const char* tempPath = "/p/.partial";
    storage.fs().remove(tempPath);

    File file = storage.fs().open(tempPath, FILE_WRITE);
    if (!file) {
        return false;
    }

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    const String url = String(API_BASE_URL) + "/photo";
    const String requestBody = String("{\"id\":\"") + photo.id.data() + "\"}";

    std::uint32_t offset = 0;
    while (offset < PANEL_BYTES) {
        const std::uint32_t end = std::min(offset + DOWNLOAD_CHUNK_BYTES, PANEL_BYTES) - 1;

        if (!beginRequest(http, client, url)) {
            break;
        }
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Range", "bytes=" + String(offset) + "-" + String(end));

        const int status = http.POST(requestBody);
        if (status != HTTP_CODE_PARTIAL_CONTENT && status != HTTP_CODE_OK) {
            http.end();
            break;
        }

        const int written = http.writeToStream(&file);
        http.end();
        if (written <= 0) {
            break;
        }
        offset += static_cast<std::uint32_t>(written);

        // A 200 means the server ignored Range and sent the whole thing.
        if (status == HTTP_CODE_OK) {
            break;
        }
    }

    const bool complete = file.size() == PANEL_BYTES;
    file.close();

    if (!complete) {
        storage.fs().remove(tempPath);
        return false;
    }

    storage.fs().remove(finalPath.data());
    return storage.fs().rename(tempPath, finalPath.data());
}

SyncResult Net::sync(Storage& storage) {
    SyncResult result;

    Manifest local;
    storage.loadManifest(local);

    Manifest remote;
    if (!fetchManifest(remote)) {
        return result;
    }

    const ManifestDiff diff = diffManifests(local, remote);

    // Deletes first: on a nearly-full filesystem the space freed here is what
    // makes room for the fetches, and swapping photos out is the common case
    // once the couple has filled the device.
    for (const PhotoEntry& photo : diff.remove) {
        if (storage.removePhoto(photo.idView())) {
            result.removed++;
        }
    }

    const std::uint32_t deadline = millis() + SYNC_TIMEOUT_MS;
    Manifest committed;

    for (const PhotoEntry& photo : remote.photos) {
        const bool needed =
            std::any_of(diff.fetch.begin(), diff.fetch.end(), [&photo](const PhotoEntry& want) {
                return want.idView() == photo.idView();
            });

        if (!needed) {
            committed.photos.push_back(photo);
            continue;
        }

        // POWER: a hard wall on radio time. Whatever landed is committed and
        // the rest waits for tomorrow — better than holding the radio up until
        // a bad connection drains the battery.
        if (static_cast<std::int32_t>(deadline - millis()) <= 0) {
            break;
        }

        if (downloadPhoto(storage, photo)) {
            committed.photos.push_back(photo);
            result.fetched++;
        }
    }

    storage.saveManifest(committed);

    result.ok = true;
    return result;
}

}  // namespace polaroid
