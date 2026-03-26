#pragma once

#include <filesystem>
#include <string>

#include "engine/assets/IFileSystem.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// StdFileSystem — desktop file system using std::filesystem.
//
// Paths are resolved relative to the root directory supplied at construction.
// On macOS/Windows desktop builds the root is typically the working directory
// or the directory containing the binary.
//
// Thread-safe: std::filesystem operations are stateless and may be called
// concurrently from worker threads.
// ---------------------------------------------------------------------------

class StdFileSystem : public IFileSystem
{
public:
    // root: base directory for all relative paths.
    // Passing "." resolves relative to the current working directory.
    explicit StdFileSystem(std::filesystem::path root = ".");

    [[nodiscard]] std::vector<uint8_t> read(std::string_view path) override;
    [[nodiscard]] bool exists(std::string_view path) override;
    [[nodiscard]] std::string resolve(std::string_view base, std::string_view relative) override;

private:
    std::filesystem::path root_;

    // Returns the canonical absolute path for the given relative-or-absolute path.
    [[nodiscard]] std::filesystem::path resolvePath(std::string_view path) const;
};

}  // namespace engine::assets
