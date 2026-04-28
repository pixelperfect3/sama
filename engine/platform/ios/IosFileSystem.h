#pragma once

#include <TargetConditionals.h>

#include <string>
#include <string_view>
#include <vector>

#include "engine/assets/IFileSystem.h"

namespace engine::platform::ios
{

// ---------------------------------------------------------------------------
// IosFileSystem — IFileSystem backed by the iOS application bundle.
//
// Reads assets via [NSBundle mainBundle].  Logical paths look the same as on
// Android: they are relative to the bundle root with forward slashes (e.g.
// "shaders/pbr.bin", "models/soldier.gltf").  When the path is absolute (it
// starts with "/") the implementation falls back to a plain POSIX read so
// callers can still load files outside the bundle (e.g. tmp downloads, the
// Documents directory).
//
// Thread-safe: NSBundle / pathForResource are documented as thread-safe and
// the std::ifstream fallback is local to each call.
// ---------------------------------------------------------------------------

class IosFileSystem : public assets::IFileSystem
{
public:
    // Constructs a file system rooted at the main bundle's resource directory.
    // The bundle path is captured eagerly (NSBundle.mainBundle is process-wide
    // and stable across the application's lifetime).
    IosFileSystem();
    ~IosFileSystem() override = default;

    // IFileSystem -----------------------------------------------------------
    [[nodiscard]] std::vector<uint8_t> read(std::string_view path) override;
    [[nodiscard]] bool exists(std::string_view path) override;
    [[nodiscard]] std::string resolve(std::string_view base, std::string_view relative) override;

private:
    // Absolute path of [NSBundle mainBundle].resourcePath.  Empty if the
    // bundle could not be located (only happens in unit-test harnesses where
    // there is no main bundle).
    std::string bundleRoot_;

    // Resolve a logical path to an absolute filesystem path.
    // Returns the path unchanged if it is already absolute.  Otherwise prepends
    // bundleRoot_/.  Returns an empty string if no bundle root is available.
    [[nodiscard]] std::string toAbsolute(std::string_view path) const;
};

}  // namespace engine::platform::ios
