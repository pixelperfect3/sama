#pragma once

#include <string>

namespace engine::assets
{

// ---------------------------------------------------------------------------
// Resolves an asset path for the active tier.
//
// Checks "<basePath>/<tier>/<relativePath>" first.  If that path does not
// exist on the filesystem, falls back to "<basePath>/<relativePath>".
//
// This is a pure utility function — it does not interact with IFileSystem
// so that the interface stays simple.
// ---------------------------------------------------------------------------

std::string resolveAssetPath(const std::string& basePath, const std::string& relativePath,
                             const std::string& tier);

}  // namespace engine::assets
