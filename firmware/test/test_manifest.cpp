#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "Manifest.h"

using namespace polaroid;
using namespace config;

void test_diff_empty_local_fetches_everything() {
    Manifest local;
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "h1", 100));
    remote.photos.push_back(makePhoto("b", "h2", 200));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(2, diff.fetch.size());
    TEST_ASSERT_EQUAL(0, diff.remove.size());
    TEST_ASSERT_EQUAL(0, diff.unchanged);
}

void test_diff_identical_fetches_nothing() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "h1", 100));
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "h1", 100));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_TRUE(diff.empty());
    TEST_ASSERT_EQUAL(1, diff.unchanged);
}

// The hash is over the packed framebuffer, so re-tuning the dither has to
// invalidate every photo even though nothing about the source images changed.
void test_diff_changed_hash_refetches() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "old", 100));
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "new", 100));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(1, diff.fetch.size());
    TEST_ASSERT_EQUAL(0, diff.remove.size());
}

void test_diff_removed_remotely_is_deleted_locally() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "h1", 100));
    local.photos.push_back(makePhoto("b", "h2", 200));
    Manifest remote;
    remote.photos.push_back(makePhoto("a", "h1", 100));

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(0, diff.fetch.size());
    TEST_ASSERT_EQUAL(1, diff.remove.size());
    TEST_ASSERT_EQUAL_STRING("b", diff.remove[0].id.data());
}

// A sync that returns nothing must not wipe the device. This is the failure
// mode that would leave the couple staring at a blank frame.
void test_diff_empty_remote_removes_all_but_fetches_none() {
    Manifest local;
    local.photos.push_back(makePhoto("a", "h1", 100));
    Manifest remote;

    ManifestDiff diff = diffManifests(local, remote);

    TEST_ASSERT_EQUAL(0, diff.fetch.size());
    TEST_ASSERT_EQUAL(1, diff.remove.size());
}

void test_next_index_wraps() {
    TEST_ASSERT_EQUAL(1, nextIndex(0, 3));
    TEST_ASSERT_EQUAL(2, nextIndex(1, 3));
    TEST_ASSERT_EQUAL(0, nextIndex(2, 3));
}

void test_next_index_survives_empty_device() {
    TEST_ASSERT_EQUAL(0, nextIndex(0, 0));
    TEST_ASSERT_EQUAL(0, nextIndex(7, 0));
}

/*
 * The manifest is newest-first, so index 0 is the newest photo. Everything
 * that wants "show what just arrived" -- a shake, a sync that deleted photos,
 * a cold boot -- sets the index to 0 rather than searching for the maximum
 * uploadedAt. This pins the ordering that makes that true.
 */
void test_manifest_order_puts_newest_first() {
    Manifest manifest;
    manifest.photos.push_back(makePhoto("newest", "h1", 300));
    manifest.photos.push_back(makePhoto("middle", "h2", 200));
    manifest.photos.push_back(makePhoto("oldest", "h3", 100));

    TEST_ASSERT_EQUAL_STRING("newest", manifest.photos[0].id.data());

    // Rotation walks backwards in time and wraps to the newest again.
    TEST_ASSERT_EQUAL(1, nextIndex(0, 3));
    TEST_ASSERT_EQUAL(2, nextIndex(1, 3));
    TEST_ASSERT_EQUAL(0, nextIndex(2, 3));
}

void test_healthy_sync_uses_the_daily_interval() {
    TEST_ASSERT_EQUAL_UINT32(SYNC_INTERVAL_SECONDS, syncInterval(0));
}

// A router outage used to retry every hour: 24 connect timeouts a day at 15 s
// and 120 mA is ~12 mAh, more than the entire rest of the daily budget.
void test_backoff_doubles_then_caps_at_the_daily_interval() {
    TEST_ASSERT_EQUAL_UINT32(SYNC_RETRY_BASE_SECONDS, syncInterval(1));
    TEST_ASSERT_EQUAL_UINT32(SYNC_RETRY_BASE_SECONDS * 2, syncInterval(2));
    TEST_ASSERT_EQUAL_UINT32(SYNC_RETRY_BASE_SECONDS * 4, syncInterval(3));

    for (std::uint8_t failures = 1; failures <= MAX_SYNC_FAILURES; failures++) {
        TEST_ASSERT_TRUE(syncInterval(failures) <= SYNC_INTERVAL_SECONDS);
    }
}

void test_backoff_never_retries_sooner_than_the_previous_step() {
    std::uint32_t previous = 0;
    for (std::uint8_t failures = 1; failures <= MAX_SYNC_FAILURES; failures++) {
        const std::uint32_t interval = syncInterval(failures);
        TEST_ASSERT_TRUE(interval >= previous);
        previous = interval;
    }
}

// The icon should mean "this has really stopped working", not "one blip".
void test_offline_icon_waits_for_several_failures() {
    TEST_ASSERT_TRUE(OFFLINE_ICON_AFTER_FAILURES >= 2);
    TEST_ASSERT_TRUE(OFFLINE_ICON_AFTER_FAILURES <= MAX_SYNC_FAILURES);
}

void runManifestTests() {
    // Unity reports whichever file main() is in otherwise.
    Unity.TestFile = __FILE__;
    RUN_TEST(test_diff_empty_local_fetches_everything);
    RUN_TEST(test_diff_identical_fetches_nothing);
    RUN_TEST(test_diff_changed_hash_refetches);
    RUN_TEST(test_diff_removed_remotely_is_deleted_locally);
    RUN_TEST(test_diff_empty_remote_removes_all_but_fetches_none);
    RUN_TEST(test_next_index_wraps);
    RUN_TEST(test_next_index_survives_empty_device);
    RUN_TEST(test_manifest_order_puts_newest_first);
    RUN_TEST(test_healthy_sync_uses_the_daily_interval);
    RUN_TEST(test_backoff_doubles_then_caps_at_the_daily_interval);
    RUN_TEST(test_backoff_never_retries_sooner_than_the_previous_step);
    RUN_TEST(test_offline_icon_waits_for_several_failures);
}
