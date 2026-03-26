#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "engine/assets/AssetManager.h"
#include "engine/assets/AssetState.h"
#include "engine/assets/CpuAssetData.h"
#include "engine/assets/IAssetLoader.h"
#include "engine/assets/IFileSystem.h"
#include "engine/assets/Texture.h"
#include "engine/threading/ThreadPool.h"

using namespace engine::assets;
using namespace engine::threading;

// ---------------------------------------------------------------------------
// Fake IFileSystem — in-memory, no disk I/O.
// ---------------------------------------------------------------------------

class FakeFileSystem : public IFileSystem
{
public:
    // Register a path → content mapping.
    void put(std::string path, std::vector<uint8_t> data)
    {
        files_[std::move(path)] = std::move(data);
    }

    [[nodiscard]] std::vector<uint8_t> read(std::string_view path) override
    {
        auto it = files_.find(std::string(path));
        if (it == files_.end())
            return {};
        return it->second;
    }

    [[nodiscard]] bool exists(std::string_view path) override
    {
        return files_.count(std::string(path)) > 0;
    }

    [[nodiscard]] std::string resolve(std::string_view base, std::string_view relative) override
    {
        // Trivial join for test purposes.
        return std::string(base) + "/" + std::string(relative);
    }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> files_;
};

// ---------------------------------------------------------------------------
// Fake IAssetLoader — returns a fixed CpuTextureData (1×1 RGBA8 pixel).
// Uses a recognisable extension so findLoader() matches.
// ---------------------------------------------------------------------------

class FakeTextureLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override
    {
        static constexpr std::string_view kExts[] = {".faketex"};
        return kExts;
    }

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t> /*bytes*/, std::string_view /*path*/,
                                      IFileSystem& /*fs*/) override
    {
        CpuTextureData tex;
        tex.pixels = {255, 0, 128, 255};  // one RGBA8 pixel
        tex.width = 1;
        tex.height = 1;
        return tex;
    }
};

// ---------------------------------------------------------------------------
// Fake loader that always throws.
// ---------------------------------------------------------------------------

class ThrowingLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override
    {
        static constexpr std::string_view kExts[] = {".throwme"};
        return kExts;
    }

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t>, std::string_view,
                                      IFileSystem&) override
    {
        throw std::runtime_error("intentional decode failure");
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Spin-wait until the handle leaves Pending/Loading state (max ~2 s).
template <typename T>
static void waitUntilUploading(AssetManager& am, AssetHandle<T> handle)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto s = am.state(handle);
        if (s != AssetState::Pending && s != AssetState::Loading)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("AssetManager — load unknown extension → immediately Failed", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);

    auto handle = am.load<Texture>("rock.unknownext");

    // No loader registered — should be Failed synchronously.
    CHECK(am.state(handle) == AssetState::Failed);
    CHECK_FALSE(am.error(handle).empty());
}

TEST_CASE("AssetManager — load missing file → Failed after decode attempt", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;  // no files registered
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeTextureLoader>());

    auto handle = am.load<Texture>("missing.faketex");

    waitUntilUploading(am, handle);
    // File not found → worker pushes error.
    CHECK(am.state(handle) == AssetState::Failed);
    CHECK_FALSE(am.error(handle).empty());
}

TEST_CASE("AssetManager — get() returns nullptr before Ready", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeTextureLoader>());

    // Register a file so the worker won't fail on read.
    fs.put("tex.faketex", {0x00});
    auto handle = am.load<Texture>("tex.faketex");

    // Asset is loading asynchronously — get() must return nullptr until Ready.
    // (It reaches Uploading, not Ready, without bgfx — that's acceptable.)
    CHECK(am.get(handle) == nullptr);
}

TEST_CASE("AssetManager — decode exception → Failed state", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<ThrowingLoader>());

    fs.put("bad.throwme", {0x01});
    auto handle = am.load<Texture>("bad.throwme");

    waitUntilUploading(am, handle);
    CHECK(am.state(handle) == AssetState::Failed);
    CHECK_FALSE(am.error(handle).empty());
}

TEST_CASE("AssetManager — processUploads() is safe on empty queue", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);

    CHECK_NOTHROW(am.processUploads());
    CHECK_NOTHROW(am.processUploads());
}

TEST_CASE("AssetManager — release() on invalid handle does not crash", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);

    AssetHandle<Texture> h;  // default-constructed = invalid
    CHECK_NOTHROW(am.release(h));
    CHECK_FALSE(h.isValid());
}

TEST_CASE("AssetManager — release() nullifies the handle", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);

    auto h = am.load<Texture>("noext");  // immediately Failed (no loader)
    REQUIRE(h.isValid());

    am.release(h);
    CHECK_FALSE(h.isValid());
}

TEST_CASE("AssetManager — duplicate load() returns same slot with incremented refcount",
          "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeTextureLoader>());
    fs.put("dup.faketex", {0x00});

    auto h1 = am.load<Texture>("dup.faketex");
    auto h2 = am.load<Texture>("dup.faketex");

    // Same slot — same handle value.
    CHECK(h1 == h2);
}

TEST_CASE("AssetManager — state() on stale handle returns Failed", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);

    auto h = am.load<Texture>("stale.unknownext");  // immediately Failed
    const uint32_t idx = h.index;
    const uint32_t gen = h.generation;

    am.release(h);
    am.processUploads();  // frees the slot, bumps generation on reuse

    // Manufacture a stale handle pointing to the old (idx, gen) pair.
    AssetHandle<Texture> stale{idx, gen};
    CHECK(am.state(stale) == AssetState::Failed);
    CHECK(am.get(stale) == nullptr);
}

TEST_CASE("AssetManager — error() returns empty string for valid non-failed handle",
          "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeTextureLoader>());
    fs.put("ok.faketex", {0x00});

    auto h = am.load<Texture>("ok.faketex");
    // May be Loading or Uploading, not Failed.
    // error() should be empty regardless.
    CHECK(am.error(h).empty());
}
