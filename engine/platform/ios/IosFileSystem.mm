#include "engine/platform/ios/IosFileSystem.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#if defined(__APPLE__) && TARGET_OS_IPHONE
#import <Foundation/Foundation.h>
#endif

#include "engine/platform/ios/IosGlobals.h"

namespace engine::platform::ios
{

namespace
{

// Read an absolute filesystem path into a byte vector.  Returns empty on any
// kind of failure (missing file, IO error, zero length).  Used by both the
// bundle-resolved and the absolute-path code paths.
std::vector<uint8_t> readAbsolute(const std::string& absPath)
{
    if (absPath.empty())
        return {};

    std::ifstream file(absPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};

    auto end = file.tellg();
    if (end <= 0)
        return {};

    file.seekg(0, std::ios::beg);
    auto size = static_cast<size_t>(end);
    std::vector<uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file)
    {
        bytes.clear();
    }
    return bytes;
}

bool existsAbsolute(const std::string& absPath)
{
    if (absPath.empty())
        return false;
    std::ifstream file(absPath, std::ios::binary);
    return file.is_open();
}

}  // namespace

IosFileSystem::IosFileSystem()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // [NSBundle mainBundle] returns a non-owning singleton; resourcePath is
    // an autoreleased NSString that lives at least until the next autorelease
    // pool drain.  We copy its UTF-8 bytes into our own std::string before any
    // pool boundary, so we can safely keep them across calls.
    NSString* path = [[NSBundle mainBundle] resourcePath];
    if (path != nil)
    {
        const char* utf8 = [path UTF8String];
        if (utf8 != nullptr)
        {
            bundleRoot_.assign(utf8);
        }
    }

    // Also publish to the global getter so other subsystems (e.g. the shader
    // loader) can look it up without holding an IosFileSystem reference.
    if (!bundleRoot_.empty())
    {
        setResourceBundlePath(bundleRoot_.c_str());
    }
#else
    // On a non-iOS build (e.g. unit tests on the host) prefer an explicit
    // override coming from setResourceBundlePath().  This keeps the class
    // testable without an actual NSBundle.
    if (const char* override_ = getResourceBundlePath())
    {
        bundleRoot_.assign(override_);
    }
#endif
}

std::vector<uint8_t> IosFileSystem::read(std::string_view path)
{
    // Pass through absolute paths unchanged.  Useful for files written into
    // the Documents directory or for unit tests that point at temp files.
    if (!path.empty() && path.front() == '/')
    {
        return readAbsolute(std::string(path));
    }

    return readAbsolute(toAbsolute(path));
}

bool IosFileSystem::exists(std::string_view path)
{
    if (!path.empty() && path.front() == '/')
    {
        return existsAbsolute(std::string(path));
    }
    return existsAbsolute(toAbsolute(path));
}

std::string IosFileSystem::resolve(std::string_view base, std::string_view relative)
{
    // Identical algorithm to AndroidFileSystem::resolve so loaders behave the
    // same on both platforms (e.g. glTF external texture lookup).  Returns a
    // logical path relative to the bundle root, never an absolute path.
    std::string baseStr(base);
    auto lastSlash = baseStr.find_last_of('/');

    std::string dir;
    if (lastSlash != std::string::npos)
    {
        dir = baseStr.substr(0, lastSlash);
    }

    std::string combined;
    if (dir.empty())
    {
        combined = std::string(relative);
    }
    else
    {
        combined = dir + "/" + std::string(relative);
    }

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

std::string IosFileSystem::toAbsolute(std::string_view path) const
{
    if (bundleRoot_.empty())
        return std::string(path);  // best-effort fallback
    std::string out;
    out.reserve(bundleRoot_.size() + 1 + path.size());
    out.assign(bundleRoot_);
    if (out.back() != '/')
        out.push_back('/');
    out.append(path.data(), path.size());
    return out;
}

}  // namespace engine::platform::ios
