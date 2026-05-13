#include "engine/platform/android/AndroidSavedState.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace engine::platform
{

namespace
{
// Process-wide cache of the activity's externalDataPath.  Set once from
// Engine::initAndroid (or from a unit test via setAndroidExternalDataPath)
// and read by readSavedState / writeSavedState.  A free function rather
// than a class member because the storage is per-process anyway — there
// is exactly one NativeActivity in any given native_app_glue process.
std::string& externalDataPathStorage()
{
    static std::string path;
    return path;
}

// Build "<externalDataPath>/<fileName>".  Returns empty string if either
// component is missing or the file name is obviously bogus (absolute path
// segment / "..").  Defensive: the contract is "small state blob" — we
// don't want to silently let a caller escape the externalDataPath.
std::string makeFullPath(const char* fileName)
{
    const std::string& base = externalDataPathStorage();
    if (base.empty() || fileName == nullptr || fileName[0] == '\0')
        return {};

    // Reject path segments that would escape externalDataPath.  These would
    // be valid `fopen()` arguments but they violate the "small file under
    // the per-app dir" contract.  Any directory traversal here would be
    // a programming error in the game.
    if (std::strchr(fileName, '/') != nullptr || std::strchr(fileName, '\\') != nullptr)
        return {};
    if (std::strcmp(fileName, "..") == 0 || std::strcmp(fileName, ".") == 0)
        return {};

    std::string out = base;
    if (out.back() != '/')
        out.push_back('/');
    out.append(fileName);
    return out;
}
}  // namespace

std::string androidExternalDataPath()
{
    return externalDataPathStorage();
}

void setAndroidExternalDataPath(std::string_view path)
{
    externalDataPathStorage().assign(path.begin(), path.end());
}

std::vector<uint8_t> readSavedState(const char* fileName)
{
    const std::string fullPath = makeFullPath(fileName);
    if (fullPath.empty())
        return {};

    std::FILE* fp = std::fopen(fullPath.c_str(), "rb");
    if (!fp)
        return {};

    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0)
    {
        std::fclose(fp);
        return {};
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    size_t read = std::fread(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);

    if (read != bytes.size())
        return {};

    return bytes;
}

bool writeSavedState(const char* fileName, std::span<const uint8_t> bytes)
{
    const std::string fullPath = makeFullPath(fileName);
    if (fullPath.empty())
        return false;

    // Write to a sibling .tmp file then rename — atomic on POSIX so a crash
    // mid-write leaves the previous snapshot intact.  Android's filesystem
    // (ext4) honours rename(2) atomicity for same-directory replacements.
    const std::string tmpPath = fullPath + ".tmp";
    std::FILE* fp = std::fopen(tmpPath.c_str(), "wb");
    if (!fp)
        return false;

    bool ok = true;
    if (!bytes.empty())
    {
        size_t written = std::fwrite(bytes.data(), 1, bytes.size(), fp);
        if (written != bytes.size())
            ok = false;
    }
    if (std::fflush(fp) != 0)
        ok = false;
    std::fclose(fp);

    if (!ok)
    {
        std::remove(tmpPath.c_str());
        return false;
    }

    if (std::rename(tmpPath.c_str(), fullPath.c_str()) != 0)
    {
        std::remove(tmpPath.c_str());
        return false;
    }

    return true;
}

}  // namespace engine::platform
