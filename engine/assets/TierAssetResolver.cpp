#include "engine/assets/TierAssetResolver.h"

#include <filesystem>

namespace engine::assets
{

std::string resolveAssetPath(const std::string& basePath, const std::string& relativePath,
                             const std::string& tier)
{
    if (!tier.empty())
    {
        namespace fs = std::filesystem;
        fs::path tierPath = fs::path(basePath) / tier / relativePath;
        if (fs::exists(tierPath))
            return tierPath.string();
    }

    // Fall back to the base path.
    return (std::filesystem::path(basePath) / relativePath).string();
}

}  // namespace engine::assets
