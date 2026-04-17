#include "engine/assets/TierAssetResolver.h"

#include <filesystem>

namespace engine::assets
{

std::string resolveAssetPath(const std::string& basePath, const std::string& relativePath,
                             const std::string& tier)
{
    namespace fs = std::filesystem;

    auto isUnderBase = [](const fs::path& resolved, const fs::path& base) -> bool
    {
        auto canonical = fs::weakly_canonical(resolved);
        auto canonicalBase = fs::weakly_canonical(base);
        auto canonicalStr = canonical.string();
        auto baseStr = canonicalBase.string();
        return canonicalStr.starts_with(baseStr);
    };

    if (!tier.empty())
    {
        fs::path tierPath = fs::path(basePath) / tier / relativePath;
        if (isUnderBase(tierPath, basePath) && fs::exists(tierPath))
            return tierPath.string();
    }

    // Fall back to the base path.
    fs::path fallback = fs::path(basePath) / relativePath;
    if (!isUnderBase(fallback, basePath))
        return basePath;  // prevent path traversal
    return fallback.string();
}

}  // namespace engine::assets
