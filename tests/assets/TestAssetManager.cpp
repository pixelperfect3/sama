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
        return std::string(base) + "/" + std::string(relative);
    }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> files_;
};

// ---------------------------------------------------------------------------
// FakeEmptyLoader — returns an empty CpuTextureData (width=0, height=0).
//
// AssetManager::upload() detects pixels.empty() and marks the asset Failed
// without calling any bgfx API, making this safe to use in tests that run
// without a bgfx context. Use this whenever a loader must be registered but
// a successful GPU upload is not the goal of the test.
// ---------------------------------------------------------------------------

class FakeEmptyLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override
    {
        static constexpr std::string_view kExts[] = {".faketex"};
        return kExts;
    }

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t>, std::string_view,
                                      IFileSystem&) override
    {
        return CpuTextureData{};  // empty — upload() rejects without touching bgfx
    }
};

// ---------------------------------------------------------------------------
// ThrowingLoader — always throws from decode(), producing a Failed asset.
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
// drainUntilDone — spin calling processUploads() until the handle leaves
// Pending/Loading, or until the deadline (max ~2 s).
//
// Unlike a bare state poll, this drives the main-thread upload drain so that
// worker errors pushed to the upload queue are actually processed.
// bgfx is not initialised in tests, so only error paths (Failed) are safe
// to drain; callers must ensure no loader returns valid pixel data.
// ---------------------------------------------------------------------------

template <typename T>
static void drainUntilDone(AssetManager& am, AssetHandle<T> handle)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        am.processUploads();
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

    // No loader registered — fails synchronously before any worker is launched.
    CHECK(am.state(handle) == AssetState::Failed);
    CHECK_FALSE(am.error(handle).empty());
}

TEST_CASE("AssetManager — load missing file → Failed after decode attempt", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;  // no files registered
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeEmptyLoader>());

    auto handle = am.load<Texture>("missing.faketex");

    // Worker sees empty read() → pushes error → processUploads() sets Failed.
    drainUntilDone(am, handle);
    CHECK(am.state(handle) == AssetState::Failed);
    CHECK_FALSE(am.error(handle).empty());
}

TEST_CASE("AssetManager — get() returns nullptr before Ready", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeEmptyLoader>());

    // File exists so the worker proceeds to decode; FakeEmptyLoader returns
    // empty pixels so upload() fails without calling any bgfx API.
    fs.put("tex.faketex", {0x00});
    auto handle = am.load<Texture>("tex.faketex");

    // Immediately after load() the asset is not yet Ready — get() must return nullptr.
    CHECK(am.get(handle) == nullptr);

    // Drain so the destructor has nothing left to process.
    drainUntilDone(am, handle);
}

TEST_CASE("AssetManager — decode exception → Failed state", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<ThrowingLoader>());

    fs.put("bad.throwme", {0x01});
    auto handle = am.load<Texture>("bad.throwme");

    drainUntilDone(am, handle);
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

TEST_CASE("AssetManager — duplicate load() returns same slot", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeEmptyLoader>());
    fs.put("dup.faketex", {0x00});

    auto h1 = am.load<Texture>("dup.faketex");
    auto h2 = am.load<Texture>("dup.faketex");

    CHECK(h1 == h2);

    // Drain before destruction (FakeEmptyLoader → Failed, no bgfx).
    drainUntilDone(am, h1);
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
    am.processUploads();  // frees the slot

    // Manufacture a stale handle pointing to the old (idx, gen) pair.
    AssetHandle<Texture> stale{idx, gen};
    CHECK(am.state(stale) == AssetState::Failed);
    CHECK(am.get(stale) == nullptr);
}

TEST_CASE("AssetManager — error() is empty for non-failed handle", "[assets][manager]")
{
    ThreadPool pool(1);
    FakeFileSystem fs;
    AssetManager am(pool, fs);
    am.registerLoader(std::make_unique<FakeEmptyLoader>());
    fs.put("ok.faketex", {0x00});

    auto h = am.load<Texture>("ok.faketex");
    // Immediately after load(), state is Loading — error() must be empty.
    CHECK(am.error(h).empty());

    // Drain before destruction.
    drainUntilDone(am, h);
}
