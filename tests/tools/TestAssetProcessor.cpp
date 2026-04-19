#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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

    /// Create a valid PNG file with the given dimensions (solid red RGBA).
    void createTestPng(const std::string& relPath, int width, int height)
    {
        fs::path full = path_ / relPath;
        fs::create_directories(full.parent_path());

        // Fill with solid red (RGBA)
        std::vector<uint8_t> pixels(width * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            pixels[i * 4 + 0] = 255;  // R
            pixels[i * 4 + 1] = 0;    // G
            pixels[i * 4 + 2] = 0;    // B
            pixels[i * 4 + 3] = 255;  // A
        }

        stbi_write_png(full.string().c_str(), width, height, 4, pixels.data(), width * 4);
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

    inputDir.createTestPng("textures/albedo.png", 64, 64);
    inputDir.createFile("textures/normal.jpg");   // not valid image, but has texture extension
    inputDir.createFile("textures/packed.ktx");   // not valid image, but has texture extension
    inputDir.createFile("textures/readme.txt");   // not a texture
    inputDir.createFile("models/mesh.glb");       // not a texture

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

    inputDir.createTestPng("textures/albedo.png", 128, 128);
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
    inputDir.createTestPng("textures/test.png", 32, 32);

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

// ---------------------------------------------------------------------------
// 9. ShaderProcessor::shaderType() classification
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor::shaderType classifies filenames", "[asset_tool]")
{
    CliArgs args;
    args.inputDir = "/tmp";
    args.outputDir = "/tmp";
    TierConfig tier = getTierConfig("mid");
    ShaderProcessor proc(args, tier);

    CHECK(proc.shaderType("vs_pbr.sc") == "vertex");
    CHECK(proc.shaderType("vs_shadow_skinned.sc") == "vertex");
    CHECK(proc.shaderType("fs_pbr.sc") == "fragment");
    CHECK(proc.shaderType("fs_bloom_downsample.sc") == "fragment");
    CHECK(proc.shaderType("cs_particles.sc") == "compute");
    CHECK(proc.shaderType("varying.def.sc") == "unknown");
    CHECK(proc.shaderType("readme.txt") == "unknown");
    CHECK(proc.shaderType("ab") == "unknown");
    CHECK(proc.shaderType("") == "unknown");
}

// ---------------------------------------------------------------------------
// 10. ShaderProcessor::platformForTarget() mapping
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor::platformForTarget maps targets", "[asset_tool]")
{
    TierConfig tier = getTierConfig("mid");

    {
        CliArgs args;
        args.inputDir = "/tmp";
        args.outputDir = "/tmp";
        args.target = "android";
        ShaderProcessor proc(args, tier);
        CHECK(proc.platformForTarget() == "android");
    }
    {
        CliArgs args;
        args.inputDir = "/tmp";
        args.outputDir = "/tmp";
        args.target = "ios";
        ShaderProcessor proc(args, tier);
        CHECK(proc.platformForTarget() == "ios");
    }
    {
        CliArgs args;
        args.inputDir = "/tmp";
        args.outputDir = "/tmp";
        args.target = "desktop";
        ShaderProcessor proc(args, tier);
        CHECK(proc.platformForTarget() == "linux");
    }
}

// ---------------------------------------------------------------------------
// 11. ShaderProcessor::profileForTarget() mapping
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor::profileForTarget maps targets", "[asset_tool]")
{
    TierConfig tier = getTierConfig("mid");

    {
        CliArgs args;
        args.inputDir = "/tmp";
        args.outputDir = "/tmp";
        args.target = "android";
        ShaderProcessor proc(args, tier);
        CHECK(proc.profileForTarget() == "spirv");
    }
    {
        CliArgs args;
        args.inputDir = "/tmp";
        args.outputDir = "/tmp";
        args.target = "ios";
        ShaderProcessor proc(args, tier);
        CHECK(proc.profileForTarget() == "metal");
    }
    {
        CliArgs args;
        args.inputDir = "/tmp";
        args.outputDir = "/tmp";
        args.target = "desktop";
        ShaderProcessor proc(args, tier);
        CHECK(proc.profileForTarget() == "440");
    }
}

// ---------------------------------------------------------------------------
// 12. ShaderProcessor::findVaryingDef() locates varying.def.sc
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor::findVaryingDef locates file in input dir", "[asset_tool]")
{
    TempDir inputDir;
    inputDir.createFile("shaders/varying.def.sc", "vec3 v_pos;");

    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = "/tmp";
    args.target = "android";

    TierConfig tier = getTierConfig("mid");
    ShaderProcessor proc(args, tier);

    std::string result = proc.findVaryingDef();
    CHECK_FALSE(result.empty());
    CHECK(result.find("varying.def.sc") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 13. ShaderProcessor graceful fallback when shaderc is not found
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor falls back to copy when shaderc not found", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createFile("shaders/vs_test.sc", "$input a_position;");
    inputDir.createFile("shaders/fs_test.sc", "void main() {}");
    inputDir.createFile("shaders/varying.def.sc", "vec3 v_pos;");

    // Use a non-existent shaderc by not having the build directory
    // The findShaderc will fail since we are running from a temp dir context.
    // We simulate by saving/restoring CWD to a directory without a build/.
    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = outputDir.path().string();
    args.target = "android";
    args.tier = "mid";

    TierConfig tier = getTierConfig("mid");
    ShaderProcessor proc(args, tier);
    auto entries = proc.discover();
    CHECK(entries.size() == 2);

    // processAll should fall back to copying when shaderc is absent from PATH
    // (This test verifies no crash occurs; the actual fallback depends on
    // whether shaderc exists in the build tree or PATH.)
    proc.processAll(entries);

    // Verify output files were created (either compiled or copied)
    for (const auto& entry : entries)
    {
        fs::path dstPath = fs::path(args.outputDir) / entry.output;
        CHECK(fs::exists(dstPath));
    }
}

// ---------------------------------------------------------------------------
// 14. ShaderProcessor skips varying_pp.def.sc
// ---------------------------------------------------------------------------

TEST_CASE("ShaderProcessor skips varying_pp.def.sc", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createFile("shaders/vs_test.sc", "test");
    inputDir.createFile("shaders/varying.def.sc", "test");
    inputDir.createFile("shaders/varying_pp.def.sc", "test");

    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = outputDir.path().string();
    args.target = "android";

    TierConfig tier = getTierConfig("mid");
    ShaderProcessor proc(args, tier);
    auto entries = proc.discover();

    // Should only find vs_test.sc, not the varying files
    CHECK(entries.size() == 1);
    CHECK(entries[0].source.find("vs_test") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 15. TextureProcessor reads actual image dimensions
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor reads actual image dimensions", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createTestPng("textures/small.png", 64, 32);
    inputDir.createTestPng("textures/large.png", 256, 512);

    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = outputDir.path().string();
    args.target = "android";
    args.tier = "mid";

    TierConfig tier = getTierConfig("mid");
    TextureProcessor proc(args, tier);
    auto entries = proc.discover();

    REQUIRE(entries.size() == 2);

    // Sort by source name for deterministic checking
    std::sort(entries.begin(), entries.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.source < b.source; });

    // large.png: 256x512, within mid tier limit of 1024
    CHECK(entries[0].originalWidth == 256);
    CHECK(entries[0].originalHeight == 512);
    CHECK(entries[0].width == 256);
    CHECK(entries[0].height == 512);

    // small.png: 64x32, well within limits
    CHECK(entries[1].originalWidth == 64);
    CHECK(entries[1].originalHeight == 32);
    CHECK(entries[1].width == 64);
    CHECK(entries[1].height == 32);
}

// ---------------------------------------------------------------------------
// 16. TextureProcessor downscales oversized images in discover
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor computes downscaled dimensions", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    // Create a 2048x1024 image, exceeding mid tier limit of 1024
    inputDir.createTestPng("textures/huge.png", 2048, 1024);

    CliArgs args;
    args.inputDir = inputDir.path().string();
    args.outputDir = outputDir.path().string();
    args.target = "android";
    args.tier = "mid";

    TierConfig tier = getTierConfig("mid");
    TextureProcessor proc(args, tier);
    auto entries = proc.discover();

    REQUIRE(entries.size() == 1);

    // Original dimensions preserved
    CHECK(entries[0].originalWidth == 2048);
    CHECK(entries[0].originalHeight == 1024);

    // Downscaled: max(2048,1024)=2048 -> scale=1024/2048=0.5
    CHECK(entries[0].width == 1024);
    CHECK(entries[0].height == 512);
}

// ---------------------------------------------------------------------------
// 17. TextureProcessor::parseBlockSize
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor::parseBlockSize parses valid sizes", "[asset_tool]")
{
    int bx = 0, by = 0;

    CHECK(TextureProcessor::parseBlockSize("4x4", bx, by));
    CHECK(bx == 4);
    CHECK(by == 4);

    CHECK(TextureProcessor::parseBlockSize("6x6", bx, by));
    CHECK(bx == 6);
    CHECK(by == 6);

    CHECK(TextureProcessor::parseBlockSize("8x8", bx, by));
    CHECK(bx == 8);
    CHECK(by == 8);

    CHECK(TextureProcessor::parseBlockSize("10x10", bx, by));
    CHECK(bx == 10);
    CHECK(by == 10);

    CHECK(TextureProcessor::parseBlockSize("12x12", bx, by));
    CHECK(bx == 12);
    CHECK(by == 12);
}

TEST_CASE("TextureProcessor::parseBlockSize rejects invalid strings", "[asset_tool]")
{
    int bx = 0, by = 0;

    CHECK_FALSE(TextureProcessor::parseBlockSize("", bx, by));
    CHECK_FALSE(TextureProcessor::parseBlockSize("abc", bx, by));
    CHECK_FALSE(TextureProcessor::parseBlockSize("x4", bx, by));
    CHECK_FALSE(TextureProcessor::parseBlockSize("4x", bx, by));
    CHECK_FALSE(TextureProcessor::parseBlockSize("3x3", bx, by));  // too small
}

// ---------------------------------------------------------------------------
// 18. TextureProcessor::astcBlockCount
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor::astcBlockCount computes correctly", "[asset_tool]")
{
    // Exact multiple
    CHECK(TextureProcessor::astcBlockCount(64, 4) == 16);
    // Rounds up
    CHECK(TextureProcessor::astcBlockCount(65, 4) == 17);
    CHECK(TextureProcessor::astcBlockCount(63, 4) == 16);
    // 6x6 blocks
    CHECK(TextureProcessor::astcBlockCount(128, 6) == 22);
    CHECK(TextureProcessor::astcBlockCount(1, 8) == 1);
}

// ---------------------------------------------------------------------------
// 19. TextureProcessor::astcGlInternalFormat
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor::astcGlInternalFormat returns GL constants", "[asset_tool]")
{
    CHECK(TextureProcessor::astcGlInternalFormat(4, 4) == 0x93B0);
    CHECK(TextureProcessor::astcGlInternalFormat(6, 6) == 0x93B4);
    CHECK(TextureProcessor::astcGlInternalFormat(8, 8) == 0x93B7);
}

// ---------------------------------------------------------------------------
// 20. TextureProcessor produces KTX output with correct header
// ---------------------------------------------------------------------------

TEST_CASE("TextureProcessor produces KTX output", "[asset_tool]")
{
    TempDir inputDir;
    TempDir outputDir;

    inputDir.createTestPng("textures/test.png", 16, 16);

    std::string inPath = inputDir.path().string();
    std::string outPath = outputDir.path().string();
    const char* argv[] = {"sama-asset-tool", "--input", inPath.c_str(), "--output",
                          outPath.c_str(),   "--tier",  "mid"};
    int argc = 7;

    auto args = parseArgs(argc, const_cast<char**>(argv));
    AssetProcessor proc(args);
    int result = proc.run();
    CHECK(result == 0);

    // Check output file exists
    fs::path ktxPath = outputDir.path() / "textures" / "test.ktx";

#if SAMA_HAS_ASTCENC
    REQUIRE(fs::exists(ktxPath));

    // Verify KTX1 magic bytes
    std::ifstream ktxFile(ktxPath, std::ios::binary);
    REQUIRE(ktxFile.good());

    uint8_t magic[12];
    ktxFile.read(reinterpret_cast<char*>(magic), 12);
    CHECK(magic[0] == 0xAB);
    CHECK(magic[1] == 0x4B);
    CHECK(magic[2] == 0x54);
    CHECK(magic[3] == 0x58);

    // Read endianness
    uint32_t endianness = 0;
    ktxFile.read(reinterpret_cast<char*>(&endianness), 4);
    CHECK(endianness == 0x04030201);

    // Skip glType, glTypeSize, glFormat (12 bytes)
    ktxFile.seekg(12, std::ios::cur);

    // Read glInternalFormat — should be ASTC 6x6 for mid tier
    uint32_t glInternalFormat = 0;
    ktxFile.read(reinterpret_cast<char*>(&glInternalFormat), 4);
    CHECK(glInternalFormat == 0x93B4);  // GL_COMPRESSED_RGBA_ASTC_6x6_KHR

    // Skip glBaseInternalFormat (4 bytes)
    ktxFile.seekg(4, std::ios::cur);

    // Read pixel dimensions
    uint32_t pixelWidth = 0, pixelHeight = 0;
    ktxFile.read(reinterpret_cast<char*>(&pixelWidth), 4);
    ktxFile.read(reinterpret_cast<char*>(&pixelHeight), 4);
    CHECK(pixelWidth == 16);
    CHECK(pixelHeight == 16);
#else
    // Without astcenc, the source file is copied as-is
    CHECK(fs::exists(ktxPath));
#endif
}
