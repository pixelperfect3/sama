#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <fstream>

#include "engine/assets/TierAssetResolver.h"
#include "engine/core/Engine.h"
#include "engine/game/ProjectConfig.h"
#include "engine/rendering/RenderSettings.h"

using namespace engine::game;
using namespace engine::rendering;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Default tiers
// ---------------------------------------------------------------------------

TEST_CASE("defaultTiers returns low/mid/high", "[game][tier]")
{
    auto tiers = defaultTiers();
    REQUIRE(tiers.size() == 3);
    REQUIRE(tiers.count("low") == 1);
    REQUIRE(tiers.count("mid") == 1);
    REQUIRE(tiers.count("high") == 1);
}

TEST_CASE("defaultTiers low tier values", "[game][tier]")
{
    auto tiers = defaultTiers();
    const auto& low = tiers.at("low");

    CHECK(low.maxTextureSize == 512);
    CHECK(low.textureCompression == "astc_8x8");
    CHECK(low.shadowMapSize == 512);
    CHECK(low.shadowCascades == 1);
    CHECK(low.maxBones == 64);
    CHECK(low.enableIBL == false);
    CHECK(low.enableSSAO == false);
    CHECK(low.enableBloom == false);
    // Bloom is off on low; steps=0 prevents a tier-override that flips
    // enableBloom=true from accidentally inheriting a 9-pass chain.  Item
    // #T3 in docs/PERF_AUDIT_2026-05-25.md.
    CHECK(low.bloomDownsampleSteps == 0);
    CHECK_THAT(low.renderScale, WithinAbs(0.75f, 1e-6f));
    CHECK(low.targetFPS == 30);
}

TEST_CASE("defaultTiers mid tier values", "[game][tier]")
{
    auto tiers = defaultTiers();
    const auto& mid = tiers.at("mid");

    CHECK(mid.maxTextureSize == 1024);
    CHECK(mid.textureCompression == "astc_6x6");
    CHECK(mid.shadowMapSize == 1024);
    CHECK(mid.shadowCascades == 2);
    CHECK(mid.maxBones == 128);
    CHECK(mid.enableIBL == true);
    CHECK(mid.enableSSAO == false);
    CHECK(mid.enableBloom == true);
    // 3 steps → 5 fullscreen passes; saves ~1-1.5 ms on mid-tier devices
    // vs the struct default of 5 (9 passes).  Item #T3 in
    // docs/PERF_AUDIT_2026-05-25.md.
    CHECK(mid.bloomDownsampleSteps == 3);
    CHECK_THAT(mid.renderScale, WithinAbs(1.0f, 1e-6f));
    CHECK(mid.targetFPS == 30);
}

TEST_CASE("defaultTiers high tier values", "[game][tier]")
{
    auto tiers = defaultTiers();
    const auto& high = tiers.at("high");

    CHECK(high.maxTextureSize == 2048);
    CHECK(high.textureCompression == "astc_4x4");
    CHECK(high.shadowMapSize == 2048);
    CHECK(high.shadowCascades == 3);
    CHECK(high.maxBones == 128);
    CHECK(high.enableIBL == true);
    CHECK(high.enableSSAO == true);
    CHECK(high.enableBloom == true);
    // Full 5-step / 9-pass chain — flagship GPUs absorb it below 0.5 ms.
    CHECK(high.bloomDownsampleSteps == 5);
    CHECK_THAT(high.renderScale, WithinAbs(1.0f, 1e-6f));
    CHECK(high.targetFPS == 60);
}

// ---------------------------------------------------------------------------
// getActiveTier
// ---------------------------------------------------------------------------

TEST_CASE("getActiveTier with valid tier name", "[game][tier]")
{
    ProjectConfig config;
    config.activeTier = "low";

    TierConfig tier = config.getActiveTier();
    CHECK(tier.maxTextureSize == 512);
    CHECK(tier.shadowMapSize == 512);
}

TEST_CASE("getActiveTier with invalid tier name falls back to mid", "[game][tier]")
{
    ProjectConfig config;
    config.activeTier = "ultra_mega";

    TierConfig tier = config.getActiveTier();
    // Should fall back to "mid" default
    CHECK(tier.maxTextureSize == 1024);
    CHECK(tier.shadowMapSize == 1024);
    CHECK(tier.shadowCascades == 2);
}

TEST_CASE("getActiveTier with empty activeTier defaults to mid", "[game][tier]")
{
    ProjectConfig config;
    // activeTier is empty by default

    TierConfig tier = config.getActiveTier();
    CHECK(tier.maxTextureSize == 1024);
    CHECK(tier.shadowCascades == 2);
}

TEST_CASE("getActiveTier prefers user-defined tiers", "[game][tier]")
{
    ProjectConfig config;
    config.activeTier = "mid";

    TierConfig custom;
    custom.maxTextureSize = 777;
    custom.shadowMapSize = 999;
    config.tiers["mid"] = custom;

    TierConfig tier = config.getActiveTier();
    CHECK(tier.maxTextureSize == 777);
    CHECK(tier.shadowMapSize == 999);
}

// ---------------------------------------------------------------------------
// tierToRenderSettings
// ---------------------------------------------------------------------------

TEST_CASE("tierToRenderSettings mapping", "[game][tier]")
{
    TierConfig tier;
    tier.shadowMapSize = 2048;
    tier.shadowCascades = 3;
    tier.enableIBL = true;
    tier.enableSSAO = true;
    tier.enableBloom = true;
    tier.enableFXAA = false;
    tier.depthPrepass = true;
    tier.renderScale = 0.8f;

    RenderSettings rs = ProjectConfig::tierToRenderSettings(tier);

    CHECK(rs.shadows.directionalRes == 2048);
    CHECK(rs.shadows.cascadeCount == 3);
    CHECK(rs.shadows.filter == ShadowFilter::PCF4x4);  // >= 2048
    CHECK(rs.lighting.iblEnabled == true);
    CHECK(rs.postProcess.ssao.enabled == true);
    CHECK(rs.postProcess.bloom.enabled == true);
    CHECK(rs.postProcess.fxaaEnabled == false);
    CHECK(rs.depthPrepassEnabled == true);
    CHECK_THAT(rs.renderScale, WithinAbs(0.8f, 1e-6f));
}

TEST_CASE("tierToRenderSettings uses Hard filter for small shadow maps", "[game][tier]")
{
    TierConfig tier;
    tier.shadowMapSize = 1024;

    RenderSettings rs = ProjectConfig::tierToRenderSettings(tier);
    CHECK(rs.shadows.filter == ShadowFilter::Hard);
}

// ---------------------------------------------------------------------------
// Bloom downsampleSteps plumbing — audit item #T3.  The TierConfig field
// must reach RenderSettings; previously tierToRenderSettings ignored it,
// so mid tier inherited the struct default (5) regardless of what the
// project asked for.  These tests pin the wire-up + the clamp at 5
// (PostProcessResources::kMaxSteps).
// ---------------------------------------------------------------------------

TEST_CASE("tierToRenderSettings forwards bloomDownsampleSteps", "[game][tier][bloom]")
{
    TierConfig tier;
    tier.enableBloom = true;
    tier.bloomDownsampleSteps = 3;

    RenderSettings rs = ProjectConfig::tierToRenderSettings(tier);
    CHECK(rs.postProcess.bloom.downsampleSteps == 3);
}

TEST_CASE("tierToRenderSettings clamps bloomDownsampleSteps at engine ceiling",
          "[game][tier][bloom]")
{
    TierConfig tier;
    tier.enableBloom = true;
    // PostProcessResources::kMaxSteps = 5; anything above must saturate
    // rather than overrun the bloomLevels_[5] array.
    tier.bloomDownsampleSteps = 12;

    RenderSettings rs = ProjectConfig::tierToRenderSettings(tier);
    CHECK(rs.postProcess.bloom.downsampleSteps == 5);
}

TEST_CASE("tierToRenderSettings clamps negative bloomDownsampleSteps to 0", "[game][tier][bloom]")
{
    TierConfig tier;
    tier.bloomDownsampleSteps = -1;  // hand-edited project.json or older format

    RenderSettings rs = ProjectConfig::tierToRenderSettings(tier);
    CHECK(rs.postProcess.bloom.downsampleSteps == 0);
}

TEST_CASE("defaultTiers bloomDownsampleSteps follow low<mid<high ordering", "[game][tier][bloom]")
{
    auto tiers = defaultTiers();
    CHECK(tiers.at("low").bloomDownsampleSteps == 0);
    CHECK(tiers.at("mid").bloomDownsampleSteps == 3);
    CHECK(tiers.at("high").bloomDownsampleSteps == 5);
    // Strict ordering — a refactor that flipped mid above high would be
    // an inversion bug; this catches it loudly.
    CHECK(tiers.at("low").bloomDownsampleSteps < tiers.at("mid").bloomDownsampleSteps);
    CHECK(tiers.at("mid").bloomDownsampleSteps < tiers.at("high").bloomDownsampleSteps);
}

// ---------------------------------------------------------------------------
// JSON parsing — tiers section
// ---------------------------------------------------------------------------

TEST_CASE("JSON parsing with tiers section", "[game][tier]")
{
    const char* json = R"({
        "name": "TierGame",
        "activeTier": "low",
        "tiers": {
            "low": {
                "maxTextureSize": 256,
                "textureCompression": "astc_8x8",
                "shadowMapSize": 256,
                "shadowCascades": 1,
                "maxBones": 32,
                "enableIBL": false,
                "enableSSAO": false,
                "enableBloom": false,
                "enableFXAA": false,
                "depthPrepass": false,
                "renderScale": 0.5,
                "targetFPS": 20,
                "bloomDownsampleSteps": 2
            }
        }
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK(config.activeTier == "low");
    REQUIRE(config.tiers.count("low") == 1);

    const auto& low = config.tiers.at("low");
    CHECK(low.maxTextureSize == 256);
    CHECK(low.textureCompression == "astc_8x8");
    CHECK(low.shadowMapSize == 256);
    CHECK(low.shadowCascades == 1);
    CHECK(low.maxBones == 32);
    CHECK(low.enableIBL == false);
    CHECK(low.enableSSAO == false);
    CHECK(low.enableBloom == false);
    CHECK(low.enableFXAA == false);
    CHECK(low.depthPrepass == false);
    CHECK_THAT(low.renderScale, WithinAbs(0.5f, 1e-6f));
    CHECK(low.targetFPS == 20);
    // Project-supplied override must reach the parsed tier struct so a
    // game can re-enable bloom on low-tier for an art test without forking
    // the engine.
    CHECK(low.bloomDownsampleSteps == 2);
}

TEST_CASE("JSON parsing with missing tiers uses defaults", "[game][tier]")
{
    const char* json = R"({
        "name": "NoTiers"
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK(config.tiers.empty());

    // getActiveTier should still return built-in mid
    TierConfig tier = config.getActiveTier();
    CHECK(tier.maxTextureSize == 1024);
    CHECK(tier.shadowCascades == 2);
}

TEST_CASE("JSON parsing with partial tier keeps defaults", "[game][tier]")
{
    const char* json = R"({
        "activeTier": "custom",
        "tiers": {
            "custom": {
                "maxTextureSize": 4096
            }
        }
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    REQUIRE(config.tiers.count("custom") == 1);
    const auto& custom = config.tiers.at("custom");

    CHECK(custom.maxTextureSize == 4096);
    // All other fields should have TierConfig defaults
    CHECK(custom.shadowMapSize == 1024);
    CHECK(custom.shadowCascades == 2);
    CHECK(custom.maxBones == 128);
    CHECK(custom.enableIBL == true);
    CHECK(custom.enableBloom == true);
    CHECK_THAT(custom.renderScale, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("JSON with activeTier override", "[game][tier]")
{
    const char* json = R"({
        "activeTier": "high"
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK(config.activeTier == "high");

    // No user tiers, so getActiveTier should use built-in high
    TierConfig tier = config.getActiveTier();
    CHECK(tier.maxTextureSize == 2048);
    CHECK(tier.shadowCascades == 3);
    CHECK(tier.targetFPS == 60);
}

// ---------------------------------------------------------------------------
// toEngineDesc with tiers
// ---------------------------------------------------------------------------

TEST_CASE("toEngineDesc applies active tier shadow settings", "[game][tier]")
{
    ProjectConfig config;
    config.activeTier = "low";
    // Legacy render config values — should be overridden by tier
    config.render.shadowResolution = 4096;
    config.render.shadowCascades = 4;

    auto desc = config.toEngineDesc();
    // low tier: shadow 512, cascades 1
    CHECK(desc.shadowResolution == 512);
    CHECK(desc.shadowCascades == 1);
}

TEST_CASE("toEngineDesc without tiers uses legacy RenderConfig", "[game][tier]")
{
    ProjectConfig config;
    // activeTier is empty, tiers map is empty
    config.render.shadowResolution = 4096;
    config.render.shadowCascades = 4;

    auto desc = config.toEngineDesc();
    CHECK(desc.shadowResolution == 4096);
    CHECK(desc.shadowCascades == 4);
}

// ---------------------------------------------------------------------------
// Asset path resolution
// ---------------------------------------------------------------------------

TEST_CASE("resolveAssetPath tier-specific path exists", "[assets][tier]")
{
    namespace fs = std::filesystem;

    // Create a temporary directory structure
    auto tmpDir = fs::temp_directory_path() / "tier_resolve_test";
    fs::create_directories(tmpDir / "mid");
    std::ofstream(tmpDir / "mid" / "texture.png").put('x');

    auto result = engine::assets::resolveAssetPath(tmpDir.string(), "texture.png", "mid");
    CHECK(result == (tmpDir / "mid" / "texture.png").string());

    fs::remove_all(tmpDir);
}

TEST_CASE("resolveAssetPath falls back to base path", "[assets][tier]")
{
    namespace fs = std::filesystem;

    auto tmpDir = fs::temp_directory_path() / "tier_resolve_test2";
    fs::create_directories(tmpDir);
    std::ofstream(tmpDir / "texture.png").put('x');

    // Tier-specific path does not exist, should fall back
    auto result = engine::assets::resolveAssetPath(tmpDir.string(), "texture.png", "nonexistent");
    CHECK(result == (tmpDir / "texture.png").string());

    fs::remove_all(tmpDir);
}

TEST_CASE("resolveAssetPath empty tier uses base path", "[assets][tier]")
{
    namespace fs = std::filesystem;

    auto tmpDir = fs::temp_directory_path() / "tier_resolve_test3";
    fs::create_directories(tmpDir);
    std::ofstream(tmpDir / "model.glb").put('x');

    auto result = engine::assets::resolveAssetPath(tmpDir.string(), "model.glb", "");
    CHECK(result == (tmpDir / "model.glb").string());

    fs::remove_all(tmpDir);
}
