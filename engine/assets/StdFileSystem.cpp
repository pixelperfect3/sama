#include "engine/assets/StdFileSystem.h"

#include <fstream>

namespace engine::assets
{

StdFileSystem::StdFileSystem(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path StdFileSystem::resolvePath(std::string_view path) const
{
    std::filesystem::path p(path);
    if (p.is_absolute())
        return p;
    return root_ / p;
}

std::vector<uint8_t> StdFileSystem::read(std::string_view path)
{
    auto fullPath = resolvePath(path);

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};

    const auto size = file.tellg();
    if (size <= 0)
        return {};

    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);

    if (!file)
        return {};

    return bytes;
}

bool StdFileSystem::exists(std::string_view path)
{
    return std::filesystem::exists(resolvePath(path));
}

std::string StdFileSystem::resolve(std::string_view base, std::string_view relative)
{
    // Resolve relative reference from the directory containing base.
    auto baseDir = std::filesystem::path(base).parent_path();
    auto resolved = std::filesystem::weakly_canonical(baseDir / relative);
    return resolved.string();
}

}  // namespace engine::assets
