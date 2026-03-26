#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "engine/assets/StdFileSystem.h"

using namespace engine::assets;

namespace
{

// RAII helper — writes a temp file, removes it on destruction.
struct TempFile
{
    std::filesystem::path path;

    TempFile(const std::filesystem::path& dir, std::string_view name,
             std::string_view content = "hello")
        : path(dir / name)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

TEST_CASE("StdFileSystem — exists() returns true for a file that exists", "[assets][fs]")
{
    auto dir = std::filesystem::temp_directory_path();
    TempFile tf(dir, "sfs_test_exists.txt");

    StdFileSystem fs(dir);
    CHECK(fs.exists("sfs_test_exists.txt"));
}

TEST_CASE("StdFileSystem — exists() returns false for a missing file", "[assets][fs]")
{
    StdFileSystem fs(std::filesystem::temp_directory_path());
    CHECK_FALSE(fs.exists("sfs_definitely_does_not_exist_xyzzy.txt"));
}

TEST_CASE("StdFileSystem — read() returns the correct bytes", "[assets][fs]")
{
    auto dir = std::filesystem::temp_directory_path();
    const std::string content = "engine_test_payload";
    TempFile tf(dir, "sfs_test_read.txt", content);

    StdFileSystem fs(dir);
    auto bytes = fs.read("sfs_test_read.txt");

    REQUIRE(bytes.size() == content.size());
    const std::string result(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    CHECK(result == content);
}

TEST_CASE("StdFileSystem — read() returns empty vector for missing file", "[assets][fs]")
{
    StdFileSystem fs(std::filesystem::temp_directory_path());
    auto bytes = fs.read("sfs_nonexistent_xyzzy.txt");
    CHECK(bytes.empty());
}

TEST_CASE("StdFileSystem — read() works with absolute path", "[assets][fs]")
{
    auto dir = std::filesystem::temp_directory_path();
    const std::string content = "absolute_path_test";
    TempFile tf(dir, "sfs_abs.txt", content);

    StdFileSystem fs(".");  // root is cwd, but we'll pass absolute path
    auto bytes = fs.read(tf.path.string());

    REQUIRE(bytes.size() == content.size());
    const std::string result(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    CHECK(result == content);
}

TEST_CASE("StdFileSystem — resolve() produces a path relative to base's directory", "[assets][fs]")
{
    StdFileSystem fs(".");

    // Simulate a glTF at "/some/dir/model.gltf" referencing "../textures/rock.png"
    const std::string result = fs.resolve("/some/dir/model.gltf", "../textures/rock.png");

    // weakly_canonical collapses the ".." — expect /some/textures/rock.png
    std::filesystem::path p(result);
    CHECK(p.filename() == "rock.png");
    CHECK(p.parent_path().filename() == "textures");
}
