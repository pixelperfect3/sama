#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "engine/io/Json.h"
#include "tools/asset_tool/AssetProcessor.h"
#include "tools/asset_tool/ShaderProcessor.h"
#include "tools/asset_tool/TextureProcessor.h"

namespace fs = std::filesystem;
using namespace engine::tools;

// ---------------------------------------------------------------------------
// Helper: create a temporary directory with test files
// ---------------------------------------------------------------------------

class TempDir
{
public:
    TempDir()
    {
        path_ = fs::temp_directory_path() /
                ("sama_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const
    {
        return path_;
    }

    void createFile(const std::string& relPath, const std::string& content = "test")
    {
        fs::path full = path_ / relPath;
        fs::create_directories(full.parent_path());
        std::ofstream out(full);
        out << content;
    }

private:
    fs::path path_;
};

// ---------------------------------------------------------------------------
// 1. CLI argument parsing
// ---------------------------------------------------------------------------

TEST_CASE("parseArgs: valid arguments", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--input", "/tmp/assets", "--output", "/tmp/out",
                          "--target",        "android", "--tier",      "high",     "--verbose",
                          "--dry-run"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    auto args = parseArgs(argc, const_cast<char**>(argv));

    CHECK(args.valid);
    CHECK(args.inputDir == "/tmp/assets");
    CHECK(args.outputDir == "/tmp/out");
    CHECK(args.target == "android");
    CHECK(args.tier == "high");
    CHECK(args.verbose);
    CHECK(args.dryRun);
    CHECK_FALSE(args.help);
}

TEST_CASE("parseArgs: help flag", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--help"};
    int argc = 2;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    CHECK(args.help);
}

TEST_CASE("parseArgs: missing required --input", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--output", "/tmp/out"};
    int argc = 3;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    CHECK_FALSE(args.valid);
    CHECK(args.errorMessage.find("--input") != std::string::npos);
}

TEST_CASE("parseArgs: missing required --output", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--input", "/tmp/assets"};
    int argc = 3;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    CHECK_FALSE(args.valid);
    CHECK(args.errorMessage.find("--output") != std::string::npos);
}

TEST_CASE("parseArgs: invalid tier", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--input", "/tmp/assets", "--output",
                          "/tmp/out",        "--tier",  "ultra"};
    int argc = 7;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    CHECK_FALSE(args.valid);
    CHECK(args.errorMessage.find("ultra") != std::string::npos);
}

TEST_CASE("parseArgs: invalid target", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--input",  "/tmp/assets", "--output",
                          "/tmp/out",        "--target", "switch"};
    int argc = 7;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    CHECK_FALSE(args.valid);
    CHECK(args.errorMessage.find("switch") != std::string::npos);
}

TEST_CASE("parseArgs: defaults", "[asset_tool]")
{
    const char* argv[] = {"sama-asset-tool", "--input", "/tmp/assets", "--output", "/tmp/out"};
    int argc = 5;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    CHECK(args.valid);
    CHECK(args.target == "android");
    CHECK(args.tier == "mid");
    CHECK_FALSE(args.verbose);
    CHECK_FALSE(args.dryRun);
}

// ---------------------------------------------------------------------------
// 2. Tier config lookup
// ---------------------------------------------------------------------------

TEST_CASE("getTierConfig: low tier", "[asset_tool]")
{
    auto cfg = getTierConfig("low");
    CHECK(cfg.name == "low");
    CHECK(cfg.maxTextureSize == 512);
    CHECK(cfg.astcBlockSize == "8x8");
    CHECK(cfg.copyModelsAsIs);
}

TEST_CASE("getTierConfig: mid tier", "[asset_tool]")
{
    auto cfg = getTierConfig("mid");
    CHECK(cfg.name == "mid");
    CHECK(cfg.maxTextureSize == 1024);
    CHECK(cfg.astcBlockSize == "6x6");
}

TEST_CASE("getTierConfig: high tier", "[asset_tool]")
{
    auto cfg = getTierConfig("high");
    CHECK(cfg.name == "high");
    CHECK(cfg.maxTextureSize == 2048);
    CHECK(cfg.astcBlockSize == "4x4");
}

TEST_CASE("getTierConfig: unknown defaults to mid", "[asset_tool]")
{
    auto cfg = getTierConfig("unknown");
    CHECK(cfg.name == "mid");
    CHECK(cfg.maxTextureSize == 1024);
}

// ---------------------------------------------------------------------------
// 3. Texture file discovery
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor discovers image files", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createFile("textures/albedo.png");
    inputDir.createFile("textures/normal.jpg");
    inputDir.createFile("textures/packed.ktx");
    inputDir.createFile("textures/readme.txt");  // not a texture
    inputDir.createFile("models/mesh.glb");      // not a texture

    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = outputDir.path().string();
    args.target = "android";
    args.tier = "mid";

    TierConfig tier = getTierConfig("mid");
    TextureProcessor proc(args, tier);
    auto entries = proc.discover();

    CHECK(entries.size() == 3);

    // All entries should be textures
    for (const auto& e : entries)
    {
        CHECK(e.type == "texture");
        CHECK(e.format == "astc_6x6");
    }
}

// ---------------------------------------------------------------------------
// 4. Shader file discovery
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor discovers .sc files", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createFile("shaders/vs_pbr.sc", "$input a_position;");
    inputDir.createFile("shaders/fs_pbr.sc", "void main() {}");
    inputDir.createFile("shaders/varying.def.sc", "vec3 v_pos;");
    inputDir.createFile("shaders/notes.txt", "not a shader");

    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = outputDir.path().string();
    args.target = "android";
    args.tier = "mid";

    TierConfig tier = getTierConfig("mid");
    ShaderProcessor proc(args, tier);
    auto entries = proc.discover();

    // Should find vs_pbr.sc and fs_pbr.sc, but not varying.def.sc
    CHECK(entries.size() == 2);

    for (const auto& e : entries)
    {
        CHECK(e.type == "shader");
        CHECK(e.format == "spirv");
        // Output should have .bin extension
        CHECK(e.output.find(".bin") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// 5. Model file discovery
// ---------------------------------------------------------------------------

TEST_CASE("AssetProcessor discovers model files", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createFile("models/Fox.glb", "binary glb data");
    inputDir.createFile("models/scene.gltf", "{}");

    std::string inPath = inputDir.path().string();
    std::string outPath = outputDir.path().string();
    const char* argv[] = {"sama-asset-tool", "--input",       inPath.c_str(),
                          "--output",        outPath.c_str(), "--dry-run"};
    int argc = 6;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    AssetProcessor proc(args);
    proc.run();

    // Check entries include models
    bool foundGlb = false;
    bool foundGltf = false;
    for (const auto& e : proc.entries())
    {
        if (e.type == "model" && e.format == "glb")
            foundGlb = true;
        if (e.type == "model" && e.format == "gltf")
            foundGltf = true;
    }
    CHECK(foundGlb);
    CHECK(foundGltf);
}

// ---------------------------------------------------------------------------
// 6. Output directory creation
// ---------------------------------------------------------------------------

TEST_CASE("AssetProcessor creates output directory", "[asset_tool]")
{
    TempDir inputDir;
    inputDir.createFile("dummy.txt");

    fs::path outDir =
        fs::temp_directory_path() /
        ("sama_out_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    CHECK_FALSE(fs::exists(outDir));

    std::string inPath = inputDir.path().string();
    std::string outStr = outDir.string();
    const char* argv[] = {"sama-asset-tool", "--input", inPath.c_str(), "--output", outStr.c_str()};
    int argc = 5;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    AssetProcessor proc(args);
    proc.run();

    CHECK(fs::exists(outDir));

    // Cleanup
    std::error_code ec;
    fs::remove_all(outDir, ec);
}

// ---------------------------------------------------------------------------
// 7. Manifest JSON generation
// ---------------------------------------------------------------------------

TEST_CASE("AssetProcessor generates manifest.json", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createFile("textures/albedo.png");
    inputDir.createFile("models/Fox.glb", "binary data");

    std::string inPath = inputDir.path().string();
    std::string outPath = outputDir.path().string();
    const char* argv[] = {"sama-asset-tool", "--input", inPath.c_str(), "--output", outPath.c_str(),
                          "--target",        "android", "--tier",       "mid"};
    int argc = 9;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    AssetProcessor proc(args);
    int result = proc.run();
    CHECK(result == 0);

    // Verify manifest.json was written
    fs::path manifestPath = outputDir.path() / "manifest.json";
    CHECK(fs::exists(manifestPath));

    // Parse the manifest and verify structure
    engine::io::JsonDocument doc;
    REQUIRE(doc.parseFile(manifestPath.string().c_str()));

    auto root = doc.root();
    CHECK(root.isObject());
    CHECK(std::string(root["platform"].getString()) == "android");
    CHECK(std::string(root["tier"].getString()) == "mid");
    CHECK(root["timestamp"].isString());
    CHECK(root["assets"].isArray());
    CHECK(root["assets"].arraySize() >= 2);  // texture + model

    // Check that each asset has required fields
    for (size_t i = 0; i < root["assets"].arraySize(); ++i)
    {
        auto asset = root["assets"][i];
        CHECK(asset["type"].isString());
        CHECK(asset["source"].isString());
        CHECK(asset["output"].isString());
        CHECK(asset["format"].isString());
    }
}

// ---------------------------------------------------------------------------
// 8. Dry-run does not create output files
// ---------------------------------------------------------------------------

TEST_CASE("AssetProcessor dry-run writes nothing", "[asset_tool]")
{
    TempDir inputDir;
    inputDir.createFile("textures/test.png");

    fs::path outDir =
        fs::temp_directory_path() /
        ("sama_dry_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    std::string inPath = inputDir.path().string();
    std::string outStr = outDir.string();
    const char* argv[] = {"sama-asset-tool", "--input",      inPath.c_str(),
                          "--output",        outStr.c_str(), "--dry-run"};
    int argc = 6;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    AssetProcessor proc(args);
    int result = proc.run();
    CHECK(result == 0);

    // Output directory should not exist in dry-run mode
    CHECK_FALSE(fs::exists(outDir));
}
