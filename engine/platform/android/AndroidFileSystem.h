#pragma once

#include <android/asset_manager.h>

#include <string>
#include <string_view>
#include <vector>

#include "engine/assets/IFileSystem.h"

namespace engine::platform
{

// ---------------------------------------------------------------------------
// AndroidFileSystem — IFileSystem backed by Android's AAssetManager.
//
// Reads assets from the APK's assets/ folder.  All paths are relative to that
// root (no leading slash).  Thread-safe: AAssetManager operations are
// thread-safe as documented by the Android NDK.
// ---------------------------------------------------------------------------

class AndroidFileSystem : public assets::IFileSystem
{
public:
    explicit AndroidFileSystem(AAssetManager* assetManager);
    ~AndroidFileSystem() override = default;

    [[nodiscard]] std::vector<uint8_t> read(std::string_view path) override;
    [[nodiscard]] bool exists(std::string_view path) override;
    [[nodiscard]] std::string resolve(std::string_view base, std::string_view relative) override;

private:
    AAssetManager* assetManager_ = nullptr;
};

}  // namespace engine::platform
