#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "engine/assets/CpuAssetData.h"

namespace engine::assets
{

class IFileSystem;

// ---------------------------------------------------------------------------
// IAssetLoader — decodes raw file bytes into CPU-side asset data.
//
// Loaders do no file I/O themselves — they receive the raw bytes from the
// AssetManager's worker thread and return a CpuAssetData variant. The
// AssetManager then enqueues the result for main-thread GPU upload.
//
// All methods must be thread-safe — multiple workers may call decode()
// concurrently with different inputs.
// ---------------------------------------------------------------------------

class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;

    // File extensions this loader handles, e.g. {".png", ".jpg"}.
    // Strings must outlive the loader (typically string literals).
    [[nodiscard]] virtual std::span<const std::string_view> extensions() const = 0;

    // Decode raw bytes into CPU-side asset data.
    //
    // bytes    — full file contents.
    // path     — original path (used for resolving relative references and
    //            for error messages).
    // fs       — file system for loading referenced files (e.g. glTF
    //            external textures).
    //
    // Returns a CpuAssetData variant on success.
    // Throws std::runtime_error (or derived) on failure — the AssetManager
    // catches it and transitions the asset to Failed.
    [[nodiscard]] virtual CpuAssetData decode(std::span<const uint8_t> bytes, std::string_view path,
                                              IFileSystem& fs) = 0;
};

}  // namespace engine::assets
