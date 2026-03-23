#include <catch2/catch_test_macros.hpp>

#include "engine/rendering/GpuFeatures.h"
#include "engine/rendering/RenderSettings.h"

using namespace engine::rendering;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isPowerOfTwo(uint16_t v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

// ---------------------------------------------------------------------------
// GpuFeatures factories
// ---------------------------------------------------------------------------

TEST_CASE("GpuFeatures::desktopDefaults — sanity", "[gpu_features]")
{
    GpuFeatures g = GpuFeatures::desktopDefaults();

    CHECK(g.isTBDR == false);
    CHECK(g.textureBC == true);
    CHECK(g.computeShaders == true);
    CHECK(g.instancing == true);
    CHECK(g.maxTextureSize >= 2048u);
    CHECK(g.maxDrawCalls > 0u);
    CHECK(g.preferredTextureFormat == GpuFeatures::TextureFormat::BC);
}

TEST_CASE("GpuFeatures::mobileDefaults — sanity", "[gpu_features]")
{
    GpuFeatures g = GpuFeatures::mobileDefaults();

    CHECK(g.isTBDR == true);
    CHECK(g.textureASTC == true);
    CHECK(g.textureBC == false);
    CHECK(g.instancing == true);
    CHECK(g.maxTextureSize >= 1024u);
    CHECK(g.maxDrawCalls > 0u);
    CHECK(g.preferredTextureFormat == GpuFeatures::TextureFormat::ASTC);
}

TEST_CASE("GpuFeatures::query — returns a struct (stub path)", "[gpu_features]")
{
    // In the stub implementation query() forwards to desktopDefaults().
    // This test just verifies it doesn't crash and returns something usable.
    GpuFeatures g = GpuFeatures::query();
    CHECK(g.maxDrawCalls > 0u);
    CHECK(g.maxTextureSize > 0u);
}

// ---------------------------------------------------------------------------
// renderSettingsLow
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsLow — shadow resolution power-of-two, single cascade",
          "[render_settings][low]")
{
    RenderSettings s = renderSettingsLow();

    CHECK(isPowerOfTwo(s.shadows.directionalRes));
    CHECK(isPowerOfTwo(s.shadows.spotRes));
    CHECK(s.shadows.directionalRes == 512);
    CHECK(s.shadows.cascadeCount == 1);
    CHECK(s.shadows.filter == ShadowFilter::Hard);
}

TEST_CASE("renderSettingsLow — expensive effects disabled", "[render_settings][low]")
{
    RenderSettings s = renderSettingsLow();

    CHECK(s.postProcess.ssao.enabled == false);
    CHECK(s.postProcess.bloom.enabled == false);
    CHECK(s.anisotropicFiltering == 1);
}

TEST_CASE("renderSettingsLow — basic fields valid", "[render_settings][low]")
{
    RenderSettings s = renderSettingsLow();

    CHECK(s.lighting.maxActiveLights > 0);
    CHECK(s.renderScale > 0.0f);
    CHECK(s.renderScale <= 1.0f);
    CHECK(s.shadows.maxDistance > 0.0f);
}

// ---------------------------------------------------------------------------
// renderSettingsMedium
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsMedium — shadow resolution and cascade count", "[render_settings][medium]")
{
    RenderSettings s = renderSettingsMedium();

    CHECK(isPowerOfTwo(s.shadows.directionalRes));
    CHECK(s.shadows.directionalRes == 1024);
    CHECK(s.shadows.cascadeCount == 2);
    CHECK(s.shadows.filter == ShadowFilter::PCF4x4);
}

TEST_CASE("renderSettingsMedium — bloom on, SSAO off", "[render_settings][medium]")
{
    RenderSettings s = renderSettingsMedium();

    CHECK(s.postProcess.ssao.enabled == false);
    CHECK(s.postProcess.bloom.enabled == true);
    CHECK(s.postProcess.bloom.downsampleSteps >= 3);
}

// ---------------------------------------------------------------------------
// renderSettingsHigh
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsHigh — shadow resolution and cascade count", "[render_settings][high]")
{
    RenderSettings s = renderSettingsHigh();

    CHECK(isPowerOfTwo(s.shadows.directionalRes));
    CHECK(s.shadows.directionalRes == 2048);
    CHECK(s.shadows.cascadeCount == 3);
    CHECK(s.shadows.filter == ShadowFilter::PCF4x4);
}

TEST_CASE("renderSettingsHigh — SSAO and bloom enabled", "[render_settings][high]")
{
    RenderSettings s = renderSettingsHigh();

    CHECK(s.postProcess.ssao.enabled == true);
    CHECK(s.postProcess.ssao.sampleCount == 16);
    CHECK(s.postProcess.bloom.enabled == true);
    CHECK(s.postProcess.bloom.downsampleSteps == 5);
}

TEST_CASE("renderSettingsHigh — depth prepass enabled (IMR default)", "[render_settings][high]")
{
    RenderSettings s = renderSettingsHigh();
    CHECK(s.depthPrepassEnabled == true);
}

// ---------------------------------------------------------------------------
// renderSettingsUltra — must exceed High on key quality knobs
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsUltra — shadow resolution higher than High", "[render_settings][ultra]")
{
    RenderSettings lo = renderSettingsHigh();
    RenderSettings hi = renderSettingsUltra();

    CHECK(hi.shadows.directionalRes > lo.shadows.directionalRes);
    CHECK(hi.shadows.directionalRes == 4096);
    CHECK(isPowerOfTwo(hi.shadows.directionalRes));
}

TEST_CASE("renderSettingsUltra — cascade count higher or equal than High",
          "[render_settings][ultra]")
{
    RenderSettings lo = renderSettingsHigh();
    RenderSettings hi = renderSettingsUltra();

    CHECK(hi.shadows.cascadeCount >= lo.shadows.cascadeCount);
    CHECK(hi.shadows.cascadeCount == 4);
}

TEST_CASE("renderSettingsUltra — PCF8x8 filter", "[render_settings][ultra]")
{
    RenderSettings s = renderSettingsUltra();
    CHECK(s.shadows.filter == ShadowFilter::PCF8x8);
}

TEST_CASE("renderSettingsUltra — SSAO sample count higher than High", "[render_settings][ultra]")
{
    RenderSettings lo = renderSettingsHigh();
    RenderSettings hi = renderSettingsUltra();

    CHECK(hi.postProcess.ssao.sampleCount > lo.postProcess.ssao.sampleCount);
    CHECK(hi.postProcess.ssao.sampleCount == 32);
}

TEST_CASE("renderSettingsUltra — bloom downsample steps >= High", "[render_settings][ultra]")
{
    RenderSettings lo = renderSettingsHigh();
    RenderSettings hi = renderSettingsUltra();

    CHECK(hi.postProcess.bloom.downsampleSteps >= lo.postProcess.bloom.downsampleSteps);
    CHECK(hi.postProcess.bloom.downsampleSteps == 5);
}

TEST_CASE("renderSettingsUltra — anisotropic filtering is maximum", "[render_settings][ultra]")
{
    RenderSettings s = renderSettingsUltra();
    CHECK(s.anisotropicFiltering == 16);
}

// ---------------------------------------------------------------------------
// renderSettingsMobile — TBDR-aware
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsMobile — depth prepass disabled", "[render_settings][mobile]")
{
    RenderSettings s = renderSettingsMobile();
    CHECK(s.depthPrepassEnabled == false);
}

TEST_CASE("renderSettingsMobile — SSAO disabled", "[render_settings][mobile]")
{
    RenderSettings s = renderSettingsMobile();
    CHECK(s.postProcess.ssao.enabled == false);
}

TEST_CASE("renderSettingsMobile — shadow resolution <= 1024", "[render_settings][mobile]")
{
    RenderSettings s = renderSettingsMobile();
    CHECK(s.shadows.directionalRes <= 1024);
    CHECK(isPowerOfTwo(s.shadows.directionalRes));
}

TEST_CASE("renderSettingsMobile — hard shadow filter (no PCF cost on tile memory)",
          "[render_settings][mobile]")
{
    RenderSettings s = renderSettingsMobile();
    CHECK(s.shadows.filter == ShadowFilter::Hard);
}

TEST_CASE("renderSettingsMobile — reduced light budget", "[render_settings][mobile]")
{
    RenderSettings mobile = renderSettingsMobile();
    RenderSettings desktop = renderSettingsHigh();
    CHECK(mobile.lighting.maxActiveLights <= desktop.lighting.maxActiveLights);
}

// ---------------------------------------------------------------------------
// renderSettingsPlatformDefault — mobile GPU (TBDR)
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsPlatformDefault(mobile) — depth prepass disabled",
          "[render_settings][platform_default]")
{
    GpuFeatures gpu = GpuFeatures::mobileDefaults();
    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.depthPrepassEnabled == false);
}

TEST_CASE("renderSettingsPlatformDefault(mobile) — shadow res capped at 1024",
          "[render_settings][platform_default]")
{
    GpuFeatures gpu = GpuFeatures::mobileDefaults();
    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.shadows.directionalRes <= 1024);
    CHECK(isPowerOfTwo(s.shadows.directionalRes));
}

TEST_CASE("renderSettingsPlatformDefault(mobile) — SSAO disabled",
          "[render_settings][platform_default]")
{
    GpuFeatures gpu = GpuFeatures::mobileDefaults();
    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.postProcess.ssao.enabled == false);
}

// ---------------------------------------------------------------------------
// renderSettingsPlatformDefault — desktop GPU (IMR)
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsPlatformDefault(desktop) — depth prepass enabled",
          "[render_settings][platform_default]")
{
    GpuFeatures gpu = GpuFeatures::desktopDefaults();
    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.depthPrepassEnabled == true);
}

TEST_CASE("renderSettingsPlatformDefault(desktop) — higher shadow res than mobile default",
          "[render_settings][platform_default]")
{
    GpuFeatures mobileGpu = GpuFeatures::mobileDefaults();
    GpuFeatures desktopGpu = GpuFeatures::desktopDefaults();

    RenderSettings mobile = renderSettingsPlatformDefault(mobileGpu);
    RenderSettings desktop = renderSettingsPlatformDefault(desktopGpu);

    CHECK(desktop.shadows.directionalRes > mobile.shadows.directionalRes);
}

TEST_CASE("renderSettingsPlatformDefault(desktop) — SSAO enabled",
          "[render_settings][platform_default]")
{
    GpuFeatures gpu = GpuFeatures::desktopDefaults();
    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.postProcess.ssao.enabled == true);
}

// ---------------------------------------------------------------------------
// renderSettingsPlatformDefault — no compute shaders → SSAO disabled
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsPlatformDefault — no compute shaders disables SSAO",
          "[render_settings][platform_default]")
{
    // Desktop GPU but without compute shader support.
    GpuFeatures gpu = GpuFeatures::desktopDefaults();
    gpu.computeShaders = false;

    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.postProcess.ssao.enabled == false);
}

TEST_CASE("renderSettingsPlatformDefault — mobile without compute shaders also disables SSAO",
          "[render_settings][platform_default]")
{
    GpuFeatures gpu = GpuFeatures::mobileDefaults();
    gpu.computeShaders = false;  // already false in mobileDefaults, but be explicit

    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.postProcess.ssao.enabled == false);
}

// ---------------------------------------------------------------------------
// renderSettingsPlatformDefault — maxTextureSize caps shadow res
// ---------------------------------------------------------------------------

TEST_CASE("renderSettingsPlatformDefault — maxTextureSize caps shadow resolution",
          "[render_settings][platform_default]")
{
    // Desktop GPU that only supports up to 1024 textures.
    GpuFeatures gpu = GpuFeatures::desktopDefaults();
    gpu.maxTextureSize = 1024;

    RenderSettings s = renderSettingsPlatformDefault(gpu);
    CHECK(s.shadows.directionalRes <= 1024);
    CHECK(s.shadows.spotRes <= 1024);
}

// ---------------------------------------------------------------------------
// Cross-preset ordering invariants
// ---------------------------------------------------------------------------

TEST_CASE("Shadow resolution ordering: Low < Medium < High < Ultra", "[render_settings][ordering]")
{
    auto lo = renderSettingsLow();
    auto med = renderSettingsMedium();
    auto hi = renderSettingsHigh();
    auto ult = renderSettingsUltra();

    CHECK(lo.shadows.directionalRes < med.shadows.directionalRes);
    CHECK(med.shadows.directionalRes < hi.shadows.directionalRes);
    CHECK(hi.shadows.directionalRes < ult.shadows.directionalRes);
}

TEST_CASE("Cascade count ordering: Low <= Medium <= High <= Ultra", "[render_settings][ordering]")
{
    auto lo = renderSettingsLow();
    auto med = renderSettingsMedium();
    auto hi = renderSettingsHigh();
    auto ult = renderSettingsUltra();

    CHECK(lo.shadows.cascadeCount <= med.shadows.cascadeCount);
    CHECK(med.shadows.cascadeCount <= hi.shadows.cascadeCount);
    CHECK(hi.shadows.cascadeCount <= ult.shadows.cascadeCount);
}

TEST_CASE("All preset shadow resolutions are powers of two", "[render_settings][validity]")
{
    for (auto s : {renderSettingsLow(), renderSettingsMedium(), renderSettingsHigh(),
                   renderSettingsUltra(), renderSettingsMobile()})
    {
        CHECK(isPowerOfTwo(s.shadows.directionalRes));
        CHECK(isPowerOfTwo(s.shadows.spotRes));
    }
}

TEST_CASE("All presets have valid cascade counts (1–4)", "[render_settings][validity]")
{
    for (auto s : {renderSettingsLow(), renderSettingsMedium(), renderSettingsHigh(),
                   renderSettingsUltra(), renderSettingsMobile()})
    {
        CHECK(s.shadows.cascadeCount >= 1);
        CHECK(s.shadows.cascadeCount <= 4);
    }
}

TEST_CASE("All presets have valid SSAO sample counts when enabled", "[render_settings][validity]")
{
    for (auto s : {renderSettingsLow(), renderSettingsMedium(), renderSettingsHigh(),
                   renderSettingsUltra(), renderSettingsMobile()})
    {
        if (s.postProcess.ssao.enabled)
        {
            CHECK(s.postProcess.ssao.sampleCount >= 8);
            CHECK(s.postProcess.ssao.sampleCount <= 32);
        }
    }
}

TEST_CASE("All presets have valid bloom downsample steps (3–5) when enabled",
          "[render_settings][validity]")
{
    for (auto s : {renderSettingsLow(), renderSettingsMedium(), renderSettingsHigh(),
                   renderSettingsUltra(), renderSettingsMobile()})
    {
        if (s.postProcess.bloom.enabled)
        {
            CHECK(s.postProcess.bloom.downsampleSteps >= 3);
            CHECK(s.postProcess.bloom.downsampleSteps <= 5);
        }
    }
}

TEST_CASE("All presets have positive maxActiveLights", "[render_settings][validity]")
{
    for (auto s : {renderSettingsLow(), renderSettingsMedium(), renderSettingsHigh(),
                   renderSettingsUltra(), renderSettingsMobile()})
    {
        CHECK(s.lighting.maxActiveLights > 0);
    }
}

TEST_CASE("All presets have renderScale in [0.5, 1.0]", "[render_settings][validity]")
{
    for (auto s : {renderSettingsLow(), renderSettingsMedium(), renderSettingsHigh(),
                   renderSettingsUltra(), renderSettingsMobile()})
    {
        CHECK(s.renderScale >= 0.5f);
        CHECK(s.renderScale <= 1.0f);
    }
}
