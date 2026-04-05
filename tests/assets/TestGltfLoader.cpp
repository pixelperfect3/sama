// GltfLoader texture loading test.
//
// Loads DamagedHelmet.glb directly via GltfLoader::decode() (no bgfx, no
// AssetManager) and verifies the decoded CPU-side texture data:
//   - correct texture count
//   - correct dimensions and pixel buffer sizes
//   - correct material → texture index mapping
//
// Each texture is written to /tmp/helmet_textures/ as a PNG so the output
// can be inspected visually.

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <variant>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma clang diagnostic pop

#include "engine/assets/CpuAssetData.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/StdFileSystem.h"

using namespace engine::assets;

TEST_CASE("GltfLoader: decode DamagedHelmet textures", "[assets][gltf]")
{
    const std::string glbPath = ENGINE_SOURCE_DIR "/assets/DamagedHelmet.glb";

    StdFileSystem fs(".");
    auto bytes = fs.read(glbPath);
    REQUIRE_FALSE(bytes.empty());

    GltfLoader loader;
    CpuAssetData result = loader.decode(bytes, glbPath, fs);

    REQUIRE(std::holds_alternative<CpuSceneData>(result));
    const auto& scene = std::get<CpuSceneData>(result);

    // -------------------------------------------------------------------------
    // Texture count and dimensions
    // -------------------------------------------------------------------------

    // DamagedHelmet has 5 images: albedo, metallicRoughness, normal, emissive, occlusion.
    REQUIRE(scene.textures.size() == 5);

    for (size_t i = 0; i < scene.textures.size(); ++i)
    {
        const auto& tex = scene.textures[i];
        INFO("texture " << i);
        CHECK(tex.width == 2048);
        CHECK(tex.height == 2048);
        CHECK(tex.pixels.size() == static_cast<size_t>(tex.width) * tex.height * 4);
    }

    // -------------------------------------------------------------------------
    // Material → texture index mapping
    // -------------------------------------------------------------------------

    REQUIRE(scene.materials.size() == 1);
    const auto& mat = scene.materials[0];

    // Expected indices into scene.textures[] (image order inside this GLB):
    //   0 = Default_albedo          (base color)
    //   1 = Default_metalRoughness  (ORM: G=roughness, B=metallic)
    //   2 = Default_emissive        (not yet wired into CpuMaterialData)
    //   3 = Default_AO              (occlusion, separate from ORM in this asset)
    //   4 = Default_normal
    CHECK(mat.albedoTexIndex == 0);
    CHECK(mat.ormTexIndex == 1);
    CHECK(mat.normalTexIndex == 4);

    // Print material scalar values so they can be visually confirmed.
    printf("\nMaterial PBR factors:\n");
    printf("  albedo factor:   (%.3f, %.3f, %.3f, %.3f)\n", mat.albedo.x, mat.albedo.y,
           mat.albedo.z, mat.albedo.w);
    printf("  roughness:       %.3f\n", mat.roughness);
    printf("  metallic:        %.3f\n", mat.metallic);
    printf("  emissiveScale:   %.3f\n", mat.emissiveScale);
    printf("  albedoTexIndex:  %d\n", mat.albedoTexIndex);
    printf("  ormTexIndex:     %d\n", mat.ormTexIndex);
    printf("  normalTexIndex:  %d\n", mat.normalTexIndex);

    // -------------------------------------------------------------------------
    // Dump textures to disk
    // -------------------------------------------------------------------------

    const std::filesystem::path outDir = "/tmp/helmet_textures";
    std::filesystem::create_directories(outDir);

    // Name each file by its role according to the image order in this GLB.
    const char* roles[5] = {"albedo", "orm", "emissive", "occlusion", "normal"};

    printf("\nWriting textures to %s/\n", outDir.c_str());

    for (size_t i = 0; i < scene.textures.size(); ++i)
    {
        const auto& tex = scene.textures[i];
        const char* role = (i < 5) ? roles[i] : "unknown";

        char filename[256];
        std::snprintf(filename, sizeof(filename), "%s/%zu_%s.png", outDir.c_str(), i, role);

        int ok = stbi_write_png(filename, static_cast<int>(tex.width), static_cast<int>(tex.height),
                                4, tex.pixels.data(), static_cast<int>(tex.width * 4));

        CHECK(ok != 0);
        printf("  [%zu] %s  %ux%u  %s\n", i, role, tex.width, tex.height,
               ok ? "OK" : "WRITE FAILED");
    }

    printf("\n");
}
