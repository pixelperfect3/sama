#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <string>

#include "engine/rendering/RenderSettingsJson.h"

using namespace engine::rendering;
using Catch::Matchers::WithinAbs;

// Helper: write a string to a temp file and return the path.
static std::string writeTempJson(const char* json)
{
    std::string path = std::string(ENGINE_SOURCE_DIR) + "/tests/rendering/_tmp_settings.json";
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(json, f);
    std::fclose(f);
    return path;
}

static void removeTempFile(const std::string& path)
{
    std::remove(path.c_str());
}

TEST_CASE("RenderSettingsJson: round-trip default settings", "[config]")
{
    auto gpu = GpuFeatures::desktopDefaults();
    RenderSettings original = renderSettingsHigh();

    std::string path =
        std::string(ENGINE_SOURCE_DIR) + "/tests/rendering/_tmp_rt_settings.json";

    REQUIRE(saveRenderSettings(original, path.c_str()));

    RenderSettings loaded = loadRenderSettings(path.c_str(), gpu);

    CHECK(loaded.shadows.enabled == original.shadows.enabled);
    CHECK(loaded.shadows.directionalRes == original.shadows.directionalRes);
    CHECK(loaded.shadows.spotRes == original.shadows.spotRes);
    CHECK(loaded.shadows.cascadeCount == original.shadows.cascadeCount);
    CHECK_THAT(loaded.shadows.maxDistance, WithinAbs(original.shadows.maxDistance, 0.01));
    CHECK(loaded.shadows.filter == original.shadows.filter);

    CHECK(loaded.lighting.maxActiveLights == original.lighting.maxActiveLights);
    CHECK(loaded.lighting.iblEnabled == original.lighting.iblEnabled);

    CHECK(loaded.postProcess.ssao.enabled == original.postProcess.ssao.enabled);
    CHECK_THAT(loaded.postProcess.ssao.radius,
               WithinAbs(original.postProcess.ssao.radius, 0.001));
    CHECK_THAT(loaded.postProcess.ssao.bias,
               WithinAbs(original.postProcess.ssao.bias, 0.001));
    CHECK(loaded.postProcess.ssao.sampleCount == original.postProcess.ssao.sampleCount);

    CHECK(loaded.postProcess.bloom.enabled == original.postProcess.bloom.enabled);
    CHECK_THAT(loaded.postProcess.bloom.threshold,
               WithinAbs(original.postProcess.bloom.threshold, 0.001));
    CHECK_THAT(loaded.postProcess.bloom.intensity,
               WithinAbs(original.postProcess.bloom.intensity, 0.001));
    CHECK(loaded.postProcess.bloom.downsampleSteps ==
          original.postProcess.bloom.downsampleSteps);

    CHECK(loaded.postProcess.fxaaEnabled == original.postProcess.fxaaEnabled);
    CHECK(loaded.postProcess.toneMapper == original.postProcess.toneMapper);
    CHECK_THAT(loaded.postProcess.exposure,
               WithinAbs(original.postProcess.exposure, 0.001));

    CHECK(loaded.depthPrepassEnabled == original.depthPrepassEnabled);
    CHECK(loaded.depthPrepassAlphaTestedOnly == original.depthPrepassAlphaTestedOnly);
    CHECK(loaded.anisotropicFiltering == original.anisotropicFiltering);
    CHECK_THAT(loaded.renderScale, WithinAbs(original.renderScale, 0.001));

    removeTempFile(path);
}

TEST_CASE("RenderSettingsJson: partial file uses defaults", "[config]")
{
    auto gpu = GpuFeatures::desktopDefaults();
    RenderSettings defaults = renderSettingsPlatformDefault(gpu);

    std::string path = writeTempJson(R"({"shadows": {"enabled": false}})");

    RenderSettings loaded = loadRenderSettings(path.c_str(), gpu);

    // The overridden field
    CHECK(loaded.shadows.enabled == false);

    // Everything else should be platform default
    CHECK(loaded.shadows.directionalRes == defaults.shadows.directionalRes);
    CHECK(loaded.shadows.cascadeCount == defaults.shadows.cascadeCount);
    CHECK(loaded.postProcess.ssao.enabled == defaults.postProcess.ssao.enabled);
    CHECK(loaded.postProcess.bloom.enabled == defaults.postProcess.bloom.enabled);
    CHECK(loaded.postProcess.toneMapper == defaults.postProcess.toneMapper);

    removeTempFile(path);
}

TEST_CASE("RenderSettingsJson: empty file uses full defaults", "[config]")
{
    auto gpu = GpuFeatures::desktopDefaults();
    RenderSettings defaults = renderSettingsPlatformDefault(gpu);

    std::string path = writeTempJson("{}");

    RenderSettings loaded = loadRenderSettings(path.c_str(), gpu);

    CHECK(loaded.shadows.enabled == defaults.shadows.enabled);
    CHECK(loaded.shadows.directionalRes == defaults.shadows.directionalRes);
    CHECK(loaded.postProcess.toneMapper == defaults.postProcess.toneMapper);
    CHECK_THAT(loaded.postProcess.exposure, WithinAbs(defaults.postProcess.exposure, 0.001));

    removeTempFile(path);
}

TEST_CASE("RenderSettingsJson: enum string mapping", "[config]")
{
    auto gpu = GpuFeatures::desktopDefaults();

    std::string path = writeTempJson(R"({
        "shadows": {"filter": "PCF8x8"},
        "postProcess": {"toneMapper": "Reinhard"}
    })");

    RenderSettings loaded = loadRenderSettings(path.c_str(), gpu);
    CHECK(loaded.shadows.filter == ShadowFilter::PCF8x8);
    CHECK(loaded.postProcess.toneMapper == ToneMapper::Reinhard);

    removeTempFile(path);
}

TEST_CASE("RenderSettingsJson: invalid enum falls back to default", "[config]")
{
    auto gpu = GpuFeatures::desktopDefaults();
    RenderSettings defaults = renderSettingsPlatformDefault(gpu);

    std::string path = writeTempJson(R"({
        "shadows": {"filter": "NonExistent"},
        "postProcess": {"toneMapper": "FakeMapper"}
    })");

    RenderSettings loaded = loadRenderSettings(path.c_str(), gpu);
    CHECK(loaded.shadows.filter == defaults.shadows.filter);
    CHECK(loaded.postProcess.toneMapper == defaults.postProcess.toneMapper);

    removeTempFile(path);
}
