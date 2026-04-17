#include "engine/platform/android/AndroidFileSystem.h"

#include <android/asset_manager.h>
#include <android/log.h>

#include <algorithm>
#include <string>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SamaEngine", __VA_ARGS__)

namespace engine::platform
{

AndroidFileSystem::AndroidFileSystem(AAssetManager* assetManager) : assetManager_(assetManager) {}

std::vector<uint8_t> AndroidFileSystem::read(std::string_view path)
{
    if (!assetManager_)
        return {};

    std::string pathStr(path);
    AAsset* asset = AAssetManager_open(assetManager_, pathStr.c_str(), AASSET_MODE_BUFFER);
    if (!asset)
    {
        LOGE("AndroidFileSystem::read — failed to open: %s", pathStr.c_str());
        return {};
    }

    auto length = AAsset_getLength(asset);
    if (length <= 0)
    {
        AAsset_close(asset);
        return {};
    }

    const void* buffer = AAsset_getBuffer(asset);
    if (!buffer)
    {
        AAsset_close(asset);
        return {};
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    std::memcpy(bytes.data(), buffer, static_cast<size_t>(length));

    AAsset_close(asset);
    return bytes;
}

bool AndroidFileSystem::exists(std::string_view path)
{
    if (!assetManager_)
        return false;

    std::string pathStr(path);
    AAsset* asset = AAssetManager_open(assetManager_, pathStr.c_str(), AASSET_MODE_STREAMING);
    if (!asset)
        return false;

    AAsset_close(asset);
    return true;
}

std::string AndroidFileSystem::resolve(std::string_view base, std::string_view relative)
{
    // Find the directory containing the base path.
    std::string baseStr(base);
    auto lastSlash = baseStr.find_last_of('/');

    std::string dir;
    if (lastSlash != std::string::npos)
    {
        dir = baseStr.substr(0, lastSlash);
    }

    // Append the relative path and simplify ".." segments.
    std::string combined;
    if (dir.empty())
    {
        combined = std::string(relative);
    }
    else
    {
        combined = dir + "/" + std::string(relative);
    }

    // Simple normalization: resolve ".." and "." segments.
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < combined.size())
    {
        auto end = combined.find('/', start);
        if (end == std::string::npos)
            end = combined.size();

        std::string segment = combined.substr(start, end - start);
        if (segment == ".." && !parts.empty())
        {
            parts.pop_back();
        }
        else if (segment != "." && !segment.empty())
        {
            parts.push_back(std::move(segment));
        }

        start = end + 1;
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
            result += '/';
        result += parts[i];
    }

    return result;
}

}  // namespace engine::platform
