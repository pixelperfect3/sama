#pragma once

#include <ankerl/unordered_dense.h>

#include <any>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "engine/assets/AssetHandle.h"
#include "engine/assets/AssetState.h"
#include "engine/assets/CpuAssetData.h"
#include "engine/assets/IAssetLoader.h"
#include "engine/assets/IFileSystem.h"
#include "engine/threading/ThreadPool.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// AssetManager — central registry for async asset loading.
//
// Usage pattern:
//   auto handle = assets.load<Texture>("textures/rock.png");
//   // ... later each frame:
//   if (assets.state(handle) == AssetState::Ready)
//       const Texture& tex = *assets.get<Texture>(handle);
//
// All load<T>() calls return immediately. File I/O and CPU decoding happen
// on worker threads. processUploads() must be called once per frame from the
// main thread to drain the queue and create bgfx GPU handles.
//
// Handles are ref-counted. release() decrements the count; when it reaches
// zero the GPU handles are destroyed at the start of the next processUploads()
// call (one-frame grace period so bgfx can flush in-flight commands).
//
// Threading contract:
//   • load(), state(), get(), error(), release() — MAIN THREAD ONLY.
//     They access internal tables without locking.
//   • processUploads() — MAIN THREAD ONLY. Must also be the bgfx submission
//     thread (i.e. call it before bgfx::frame() / renderer.endFrame()).
//   • Worker threads only call pushUpload() / pushError(), which are
//     mutex-protected. No other internal state is touched from workers.
//
// Destruction ordering:
//   AssetManager::~AssetManager calls processUploads(), which creates or
//   destroys bgfx handles. The bgfx context (Renderer) MUST still be alive
//   when AssetManager is destroyed. Ensure AssetManager is constructed after
//   Renderer so that it is destroyed first (LIFO stack order).
// ---------------------------------------------------------------------------

class AssetManager
{
public:
    AssetManager(threading::ThreadPool& pool, IFileSystem& fs);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // ------------------------------------------------------------------
    // Loader registration — call before any load<T>() for that extension.
    // ------------------------------------------------------------------

    void registerLoader(std::unique_ptr<IAssetLoader> loader);

    // ------------------------------------------------------------------
    // Load API
    //
    // Returns a handle immediately. The asset may still be loading.
    // If the same path is already tracked the ref count is incremented
    // and the existing handle is returned.
    // ------------------------------------------------------------------

    template <typename T>
    [[nodiscard]] AssetHandle<T> load(std::string_view path);

    // ------------------------------------------------------------------
    // Query
    // ------------------------------------------------------------------

    template <typename T>
    [[nodiscard]] AssetState state(AssetHandle<T> handle) const;

    // Returns nullptr if handle is invalid, stale, or not yet Ready.
    template <typename T>
    [[nodiscard]] const T* get(AssetHandle<T> handle) const;

    // Returns the error string for a Failed asset, empty string otherwise.
    template <typename T>
    [[nodiscard]] const std::string& error(AssetHandle<T> handle) const;

    // ------------------------------------------------------------------
    // Lifetime
    // ------------------------------------------------------------------

    // Decrement ref count. Nullifies the handle.
    // When ref count hits 0 the slot is marked for deferred destruction.
    template <typename T>
    void release(AssetHandle<T>& handle);

    // ------------------------------------------------------------------
    // Main-thread frame tick
    //
    // Drains the upload queue: creates bgfx GPU handles and marks assets
    // Ready. Also frees slots that were released the previous frame.
    // Call once per frame before bgfx::frame() / renderer.endFrame() so
    // that newly created GPU handles are submitted in the same frame.
    // ------------------------------------------------------------------

    void processUploads();

private:
    // ------------------------------------------------------------------
    // Internal slot record
    // ------------------------------------------------------------------

    struct Record
    {
        std::string path;
        AssetState state = AssetState::Pending;
        uint32_t refCount = 0;
        uint32_t generation = 1;
        std::any payload;   // set on Ready; type matches the loaded asset type
        std::string error;  // set on Failed
    };

    // ------------------------------------------------------------------
    // Upload queue entry — pushed by worker threads, drained by main thread
    // ------------------------------------------------------------------

    struct UploadRequest
    {
        uint32_t index;
        uint32_t generation;
        // Holds decoded CPU data on success, or an error string on failure.
        // CpuAssetData is a forward-declared variant defined in CpuAssetData.h.
        std::variant<CpuAssetData, std::string> result;
    };

    // ------------------------------------------------------------------
    // Slot management
    // ------------------------------------------------------------------

    uint32_t allocSlot(std::string_view path);
    void scheduleDestroy(uint32_t index);

    [[nodiscard]] bool isHandleValid(uint32_t index, uint32_t generation) const;

    [[nodiscard]] IAssetLoader* findLoader(std::string_view path) const;

    void pushUpload(uint32_t index, uint32_t generation, CpuAssetData data);
    void pushError(uint32_t index, uint32_t generation, std::string msg);

    void uploadOne(UploadRequest& req);

    // Per-type upload: called from uploadOne() via std::visit.
    // Each overload creates bgfx handles and stores the result in rec.payload.
    void upload(Record& rec, CpuTextureData&& data);
    void upload(Record& rec, CpuSceneData&& data);
    void upload(Record& rec, CpuCompressedTextureData&& data);

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    threading::ThreadPool& pool_;
    IFileSystem& fs_;

    // records_[0] is an invalid sentinel; real slots start at index 1.
    std::vector<Record> records_;
    std::vector<uint32_t> freeList_;

    ankerl::unordered_dense::map<std::string, uint32_t> pathToSlot_;

    // Slots released last frame — freed at the start of processUploads().
    std::vector<uint32_t> pendingFree_;

    // Upload queue — written by workers, drained by main thread.
    mutable std::mutex uploadMutex_;
    std::vector<UploadRequest> uploadQueue_;

    std::vector<std::unique_ptr<IAssetLoader>> loaders_;

    static const std::string kEmptyError;
};

// ---------------------------------------------------------------------------
// Template implementations
// ---------------------------------------------------------------------------

template <typename T>
AssetHandle<T> AssetManager::load(std::string_view path)
{
    // Deduplicate: return existing handle if path is already tracked.
    auto it = pathToSlot_.find(std::string(path));
    if (it != pathToSlot_.end())
    {
        uint32_t idx = it->second;
        records_[idx].refCount++;
        return AssetHandle<T>{idx, records_[idx].generation};
    }

    // Allocate a new slot.
    const uint32_t idx = allocSlot(path);
    Record& rec = records_[idx];

    IAssetLoader* loader = findLoader(path);
    if (!loader)
    {
        rec.state = AssetState::Failed;
        rec.error = "No loader registered for: " + std::string(path);
        return AssetHandle<T>{idx, rec.generation};
    }

    rec.state = AssetState::Loading;

    // Capture by value — the record vector may reallocate.
    const uint32_t gen = rec.generation;
    const std::string pathStr(path);

    pool_.submit(
        [this, idx, gen, pathStr, loader]()
        {
            auto bytes = fs_.read(pathStr);
            if (bytes.empty())
            {
                pushError(idx, gen, "File not found: " + pathStr);
                return;
            }
            try
            {
                CpuAssetData data = loader->decode(bytes, pathStr, fs_);
                pushUpload(idx, gen, std::move(data));
            }
            catch (const std::exception& e)
            {
                pushError(idx, gen, e.what());
            }
        });

    return AssetHandle<T>{idx, gen};
}

template <typename T>
AssetState AssetManager::state(AssetHandle<T> handle) const
{
    if (!isHandleValid(handle.index, handle.generation))
        return AssetState::Failed;
    return records_[handle.index].state;
}

template <typename T>
const T* AssetManager::get(AssetHandle<T> handle) const
{
    if (!isHandleValid(handle.index, handle.generation))
        return nullptr;
    const Record& rec = records_[handle.index];
    if (rec.state != AssetState::Ready)
        return nullptr;
    return std::any_cast<T>(&rec.payload);
}

template <typename T>
const std::string& AssetManager::error(AssetHandle<T> handle) const
{
    if (!isHandleValid(handle.index, handle.generation))
        return kEmptyError;
    return records_[handle.index].error;
}

template <typename T>
void AssetManager::release(AssetHandle<T>& handle)
{
    if (!handle.isValid())
        return;
    if (!isHandleValid(handle.index, handle.generation))
    {
        handle = {};
        return;
    }

    Record& rec = records_[handle.index];
    if (rec.refCount > 0)
        rec.refCount--;

    if (rec.refCount == 0)
        scheduleDestroy(handle.index);

    handle = {};
}

}  // namespace engine::assets
