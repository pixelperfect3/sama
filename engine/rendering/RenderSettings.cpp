#include "engine/rendering/RenderSettings.h"

#include <algorithm>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// renderSettingsLow
//
// Designed for weak integrated GPUs and old mobile hardware.
// Hard shadows, single cascade, no SSAO, no bloom, aniso x1.
// ---------------------------------------------------------------------------

RenderSettings renderSettingsLow()
{
    RenderSettings s;

    s.shadows.enabled = true;
    s.shadows.directionalRes = 512;
    s.shadows.spotRes = 512;
    s.shadows.cascadeCount = 1;
    s.shadows.maxDistance = 60.0f;
    s.shadows.filter = ShadowFilter::Hard;

    s.lighting.maxActiveLights = 64;
    s.lighting.iblEnabled = true;

    s.postProcess.ssao.enabled = false;
    s.postProcess.bloom.enabled = false;
    s.postProcess.fxaaEnabled = true;
    s.postProcess.toneMapper = ToneMapper::Reinhard;
    s.postProcess.exposure = 1.0f;

    s.depthPrepassEnabled = true;
    s.depthPrepassAlphaTestedOnly = false;
    s.anisotropicFiltering = 1;
    s.renderScale = 1.0f;

    return s;
}

// ---------------------------------------------------------------------------
// renderSettingsMedium
//
// Balanced quality for mid-range desktops and newer mobile flagships.
// 1024 directional shadow, PCF4x4, 2 cascades, SSAO off (costly), bloom on.
// ---------------------------------------------------------------------------

RenderSettings renderSettingsMedium()
{
    RenderSettings s;

    s.shadows.enabled = true;
    s.shadows.directionalRes = 1024;
    s.shadows.spotRes = 512;
    s.shadows.cascadeCount = 2;
    s.shadows.maxDistance = 100.0f;
    s.shadows.filter = ShadowFilter::PCF4x4;

    s.lighting.maxActiveLights = 128;
    s.lighting.iblEnabled = true;

    s.postProcess.ssao.enabled = false;
    s.postProcess.bloom.enabled = true;
    s.postProcess.bloom.downsampleSteps = 3;
    s.postProcess.bloom.intensity = 0.04f;
    s.postProcess.bloom.threshold = 1.0f;
    s.postProcess.fxaaEnabled = true;
    s.postProcess.toneMapper = ToneMapper::ACES;
    s.postProcess.exposure = 1.0f;

    s.depthPrepassEnabled = true;
    s.depthPrepassAlphaTestedOnly = false;
    s.anisotropicFiltering = 4;
    s.renderScale = 1.0f;

    return s;
}

// ---------------------------------------------------------------------------
// renderSettingsHigh
//
// Default for capable desktop GPUs (Mac desktop, Windows mid–high tier).
// 2048 directional shadow, PCF4x4, 3 cascades, SSAO 16 samples, bloom 5 steps.
// ---------------------------------------------------------------------------

RenderSettings renderSettingsHigh()
{
    RenderSettings s;

    s.shadows.enabled = true;
    s.shadows.directionalRes = 2048;
    s.shadows.spotRes = 1024;
    s.shadows.cascadeCount = 3;
    s.shadows.maxDistance = 150.0f;
    s.shadows.filter = ShadowFilter::PCF4x4;

    s.lighting.maxActiveLights = 256;
    s.lighting.iblEnabled = true;

    s.postProcess.ssao.enabled = true;
    s.postProcess.ssao.radius = 0.5f;
    s.postProcess.ssao.bias = 0.025f;
    s.postProcess.ssao.sampleCount = 16;
    s.postProcess.bloom.enabled = true;
    s.postProcess.bloom.downsampleSteps = 5;
    s.postProcess.bloom.intensity = 0.04f;
    s.postProcess.bloom.threshold = 1.0f;
    s.postProcess.fxaaEnabled = true;
    s.postProcess.toneMapper = ToneMapper::ACES;
    s.postProcess.exposure = 1.0f;

    s.depthPrepassEnabled = true;
    s.depthPrepassAlphaTestedOnly = false;
    s.anisotropicFiltering = 8;
    s.renderScale = 1.0f;

    return s;
}

// ---------------------------------------------------------------------------
// renderSettingsUltra
//
// Maximum quality for high-end desktops — no GPU budget concerns.
// 4096 directional shadow, PCF8x8, 4 cascades, SSAO 32 samples, bloom 5 steps.
// ---------------------------------------------------------------------------

RenderSettings renderSettingsUltra()
{
    RenderSettings s;

    s.shadows.enabled = true;
    s.shadows.directionalRes = 4096;
    s.shadows.spotRes = 2048;
    s.shadows.cascadeCount = 4;
    s.shadows.maxDistance = 200.0f;
    s.shadows.filter = ShadowFilter::PCF8x8;

    s.lighting.maxActiveLights = 512;
    s.lighting.iblEnabled = true;

    s.postProcess.ssao.enabled = true;
    s.postProcess.ssao.radius = 0.5f;
    s.postProcess.ssao.bias = 0.025f;
    s.postProcess.ssao.sampleCount = 32;
    s.postProcess.bloom.enabled = true;
    s.postProcess.bloom.downsampleSteps = 5;
    s.postProcess.bloom.intensity = 0.04f;
    s.postProcess.bloom.threshold = 1.0f;
    s.postProcess.fxaaEnabled = true;
    s.postProcess.toneMapper = ToneMapper::ACES;
    s.postProcess.exposure = 1.0f;

    s.depthPrepassEnabled = true;
    s.depthPrepassAlphaTestedOnly = false;
    s.anisotropicFiltering = 16;
    s.renderScale = 1.0f;

    return s;
}

// ---------------------------------------------------------------------------
// renderSettingsMobile
//
// TBDR-aware mobile preset (platform default for iOS; conservative Android).
// No depth prepass (harmful on TBDR), 1024 shadow, hard filter, SSAO off.
// Reduced light budget to fit mobile memory constraints.
// ---------------------------------------------------------------------------

RenderSettings renderSettingsMobile()
{
    RenderSettings s;

    s.shadows.enabled = true;
    s.shadows.directionalRes = 1024;
    s.shadows.spotRes = 512;
    s.shadows.cascadeCount = 2;
    s.shadows.maxDistance = 80.0f;
    s.shadows.filter = ShadowFilter::Hard;

    s.lighting.maxActiveLights = 64;
    s.lighting.iblEnabled = true;

    s.postProcess.ssao.enabled = false;
    s.postProcess.bloom.enabled = true;
    s.postProcess.bloom.downsampleSteps = 3;
    s.postProcess.bloom.intensity = 0.04f;
    s.postProcess.bloom.threshold = 1.0f;
    s.postProcess.fxaaEnabled = true;
    s.postProcess.toneMapper = ToneMapper::ACES;
    s.postProcess.exposure = 1.0f;

    // Depth prepass is harmful on TBDR — HSR does this for free.
    s.depthPrepassEnabled = false;
    s.depthPrepassAlphaTestedOnly = false;
    s.anisotropicFiltering = 4;
    s.renderScale = 1.0f;

    return s;
}

// ---------------------------------------------------------------------------
// renderSettingsPlatformDefault
//
// Adapts quality to the detected GPU capabilities.
//
// TBDR path (mobile / Apple Silicon):
//   - Base: renderSettingsMobile()
//   - Cap directionalRes to 1024 (TBDR shadow maps are memory-bandwidth bound)
//   - Disable depthPrepassEnabled (TBDR HSR handles occlusion for free)
//   - Disable SSAO (requires compute; also costly on tile memory)
//
// IMR path (discrete desktop):
//   - Base: renderSettingsHigh()
//
// Shared rule:
//   - !computeShaders → disable SSAO regardless of architecture
// ---------------------------------------------------------------------------

RenderSettings renderSettingsPlatformDefault(const GpuFeatures& gpu)
{
    RenderSettings s = gpu.isTBDR ? renderSettingsMobile() : renderSettingsHigh();

    if (gpu.isTBDR)
    {
        // Enforce TBDR constraints even if the base preset changes in the future.
        s.depthPrepassEnabled = false;
        s.shadows.directionalRes = std::min(s.shadows.directionalRes, static_cast<uint16_t>(1024));
        s.postProcess.ssao.enabled = false;
    }

    if (!gpu.computeShaders)
    {
        // SSAO requires the compute path for the AO kernel dispatch.
        s.postProcess.ssao.enabled = false;
    }

    // Cap shadow map resolution to what the device actually supports.
    if (gpu.maxTextureSize > 0)
    {
        auto capRes = [&](uint16_t res) -> uint16_t
        { return static_cast<uint16_t>(std::min(static_cast<uint32_t>(res), gpu.maxTextureSize)); };
        s.shadows.directionalRes = capRes(s.shadows.directionalRes);
        s.shadows.spotRes = capRes(s.shadows.spotRes);
    }

    return s;
}

}  // namespace engine::rendering
