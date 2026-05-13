// Host-side unit tests for AndroidSavedState.  The header / cpp avoid all
// `<android/...>` includes precisely so the read/write/path-sanitisation
// logic can be exercised on the dev Mac without an AVD.  We point
// `setAndroidExternalDataPath` at a Catch2-managed temp directory and round
// trip a small POD blob.
//
// On Android the same code paths fire — the only difference is that
// `Engine::initAndroid` calls `setAndroidExternalDataPath` with
// `android_app::activity->externalDataPath` instead of a unit-test tmp dir.

#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "engine/platform/android/AndroidSavedState.h"

namespace fs = std::filesystem;

namespace
{
// Build a unique temp directory under the OS tmpdir, register cleanup, and
// point the SavedState API at it.  Returning the path so individual tests
// can probe file existence directly when they need to.
fs::path makeTempDataPath()
{
    fs::path dir =
        fs::temp_directory_path() / ("sama_saved_state_" + std::to_string(::getpid()) + "_" +
                                     std::to_string(static_cast<long>(std::rand())));
    fs::create_directories(dir);
    engine::platform::setAndroidExternalDataPath(dir.string());
    return dir;
}

void cleanupTempDataPath(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    engine::platform::setAndroidExternalDataPath("");
}
}  // namespace

TEST_CASE("AndroidSavedState — externalDataPath defaults to empty", "[platform][android]")
{
    // Reset to a known state — the previous test may have set this.
    engine::platform::setAndroidExternalDataPath("");
    CHECK(engine::platform::androidExternalDataPath().empty());
}

TEST_CASE("AndroidSavedState — read returns empty when path is unset", "[platform][android]")
{
    engine::platform::setAndroidExternalDataPath("");
    auto bytes = engine::platform::readSavedState("state.bin");
    CHECK(bytes.empty());
}

TEST_CASE("AndroidSavedState — write returns false when path is unset", "[platform][android]")
{
    engine::platform::setAndroidExternalDataPath("");
    uint8_t payload[] = {1, 2, 3};
    CHECK_FALSE(engine::platform::writeSavedState("state.bin", payload));
}

TEST_CASE("AndroidSavedState — round-trip write then read", "[platform][android]")
{
    auto dir = makeTempDataPath();
    const uint8_t kPayload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42, 0x00, 0xFF};

    REQUIRE(engine::platform::writeSavedState("game.state", kPayload));

    // File should exist on disk under the configured externalDataPath.
    CHECK(fs::exists(dir / "game.state"));

    auto bytes = engine::platform::readSavedState("game.state");
    REQUIRE(bytes.size() == sizeof(kPayload));
    for (size_t i = 0; i < bytes.size(); ++i)
        CHECK(bytes[i] == kPayload[i]);

    cleanupTempDataPath(dir);
}

TEST_CASE("AndroidSavedState — read returns empty when file missing", "[platform][android]")
{
    auto dir = makeTempDataPath();
    auto bytes = engine::platform::readSavedState("nonexistent.bin");
    CHECK(bytes.empty());
    cleanupTempDataPath(dir);
}

TEST_CASE("AndroidSavedState — overwrite is atomic-ish", "[platform][android]")
{
    auto dir = makeTempDataPath();
    const uint8_t first[] = {1, 1, 1, 1};
    const uint8_t second[] = {2, 2};

    REQUIRE(engine::platform::writeSavedState("over.bin", first));
    REQUIRE(engine::platform::writeSavedState("over.bin", second));

    // After two writes there is no .tmp sidecar — the rename completed.
    CHECK_FALSE(fs::exists(dir / "over.bin.tmp"));

    auto bytes = engine::platform::readSavedState("over.bin");
    REQUIRE(bytes.size() == 2);
    CHECK(bytes[0] == 2);
    CHECK(bytes[1] == 2);

    cleanupTempDataPath(dir);
}

TEST_CASE("AndroidSavedState — rejects path traversal", "[platform][android]")
{
    auto dir = makeTempDataPath();
    const uint8_t payload[] = {7};

    // Slashes / backslashes / .. — all rejected at the helper boundary so
    // games can't accidentally write outside externalDataPath.
    CHECK_FALSE(engine::platform::writeSavedState("../escape.bin", payload));
    CHECK_FALSE(engine::platform::writeSavedState("sub/dir.bin", payload));
    CHECK_FALSE(engine::platform::writeSavedState("..", payload));
    CHECK_FALSE(engine::platform::writeSavedState(".", payload));
    CHECK_FALSE(engine::platform::writeSavedState("", payload));

    CHECK(engine::platform::readSavedState("../escape.bin").empty());
    CHECK(engine::platform::readSavedState("sub/dir.bin").empty());

    cleanupTempDataPath(dir);
}

TEST_CASE("AndroidSavedState — empty payload writes zero-byte file then reads empty",
          "[platform][android]")
{
    auto dir = makeTempDataPath();

    // Writing an empty span succeeds and produces a 0-byte file.  readSavedState
    // returns empty (size 0 is intentionally indistinguishable from "missing"
    // — games should check `fileExists` themselves if they need to disambiguate,
    // but the typical pattern is "any state-blob is non-empty by construction").
    REQUIRE(engine::platform::writeSavedState("empty.bin", std::span<const uint8_t>{}));
    CHECK(fs::exists(dir / "empty.bin"));

    auto bytes = engine::platform::readSavedState("empty.bin");
    CHECK(bytes.empty());

    cleanupTempDataPath(dir);
}
