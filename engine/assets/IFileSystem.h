#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets
{

// ---------------------------------------------------------------------------
// IFileSystem — platform-agnostic file access.
//
// All asset loaders go through this interface. Game code and loaders never
// construct platform-specific paths directly.
//
// Implementations:
//   StdFileSystem  — desktop (macOS, Windows, Linux) via std::filesystem
//   BundleFileSystem — iOS app bundle (future)
//   AAssetFileSystem — Android AAssetManager (future)
//
// All methods are called from worker threads and must be thread-safe.
// ---------------------------------------------------------------------------

class IFileSystem
{
public:
    virtual ~IFileSystem() = default;

    // Read the entire file at path into a byte vector.
    // Returns an empty vector if the file does not exist or cannot be read.
    [[nodiscard]] virtual std::vector<uint8_t> read(std::string_view path) = 0;

    // Return true if a file exists at path without reading it.
    [[nodiscard]] virtual bool exists(std::string_view path) = 0;

    // Resolve a relative reference from a base path.
    // Used by loaders to find resources referenced inside asset files
    // (e.g. glTF external texture paths).
    //   base     = "assets/models/soldier.gltf"
    //   relative = "../textures/albedo.png"
    //   result   = "assets/textures/albedo.png"
    [[nodiscard]] virtual std::string resolve(std::string_view base, std::string_view relative) = 0;
};

}  // namespace engine::assets
