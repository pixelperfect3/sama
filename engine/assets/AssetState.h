#pragma once

#include <cstdint>

namespace engine::assets
{

// ---------------------------------------------------------------------------
// AssetState — lifecycle state of a single asset slot.
//
// Transitions:
//   Pending   → Loading   (worker thread starts)
//   Loading   → Uploading (CPU decode complete, queued for GPU upload)
//   Uploading → Ready     (main thread GPU upload complete)
//   Loading   → Failed    (file not found, decode error)
//   Uploading → Failed    (GPU upload error — rare)
// ---------------------------------------------------------------------------

enum class AssetState : uint8_t
{
    Pending,    // queued, worker not yet started
    Loading,    // worker thread is decoding
    Uploading,  // CPU decode done, waiting for main-thread GPU upload
    Ready,      // GPU handles live, asset usable via AssetManager::get<T>()
    Failed,     // load or decode error; error string available via AssetManager::error()
};

}  // namespace engine::assets
