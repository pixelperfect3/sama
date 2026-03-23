#pragma once

#include <cstdint>

#include "engine/rendering/GpuFeatures.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Enumerations shared across sub-structs
// ---------------------------------------------------------------------------

enum class ShadowFilter : uint8_t
{
    Hard,    ///< Single tap — fastest, aliased
    PCF4x4,  ///< 4x4 percentage-closer filter — balanced
    PCF8x8   ///< 8x8 percentage-closer filter — highest quality
};

enum class ToneMapper : uint8_t
{
    ACES,       ///< ACES filmic (default)
    Reinhard,   ///< Simple Reinhard
    Uncharted2  ///< Uncharted 2 / Hable
};

// ---------------------------------------------------------------------------
// ShadowSettings
// ---------------------------------------------------------------------------

struct ShadowSettings
{
    bool enabled = true;
    uint16_t directionalRes = 2048;  ///< Power of two: 512 / 1024 / 2048 / 4096
    uint16_t spotRes = 1024;         ///< Power of two: 512 / 1024 / 2048
    uint8_t cascadeCount = 3;        ///< 1 – 4 cascades for directional CSM
    float maxDistance = 150.0f;
    ShadowFilter filter = ShadowFilter::PCF4x4;
};

// ---------------------------------------------------------------------------
// SsaoSettings
// ---------------------------------------------------------------------------

struct SsaoSettings
{
    bool enabled = true;
    float radius = 0.5f;
    float bias = 0.025f;
    uint8_t sampleCount = 16;  ///< 8 / 16 / 32
};

// ---------------------------------------------------------------------------
// BloomSettings
// ---------------------------------------------------------------------------

struct BloomSettings
{
    bool enabled = true;
    float threshold = 1.0f;
    float intensity = 0.04f;
    uint8_t downsampleSteps = 5;  ///< 3 / 4 / 5
};

// ---------------------------------------------------------------------------
// PostProcessSettings
// ---------------------------------------------------------------------------

struct PostProcessSettings
{
    SsaoSettings ssao;
    BloomSettings bloom;
    bool fxaaEnabled = true;
    ToneMapper toneMapper = ToneMapper::ACES;
    float exposure = 1.0f;
};

// ---------------------------------------------------------------------------
// LightingSettings
// ---------------------------------------------------------------------------

struct LightingSettings
{
    uint16_t maxActiveLights = 256;  ///< Capped by cluster uniform buffer size
    bool iblEnabled = true;
};

// ---------------------------------------------------------------------------
// RenderSettings — top-level aggregate
// ---------------------------------------------------------------------------

struct RenderSettings
{
    ShadowSettings shadows;
    LightingSettings lighting;
    PostProcessSettings postProcess;

    /// Depth-only prepass before opaque pass.
    /// Beneficial on IMR (desktop) — saves fragment work on occluded geometry.
    /// Harmful on TBDR (mobile) — doubles vertex work and forces tile resolves.
    bool depthPrepassEnabled = true;

    /// TBDR exception: depth prepass for alpha-tested geometry only (foliage,
    /// fences, hair).  Safe on TBDR because it covers a narrow subset.
    bool depthPrepassAlphaTestedOnly = false;

    uint8_t anisotropicFiltering = 8;  ///< 1 / 4 / 8 / 16
    float renderScale = 1.0f;          ///< 0.5 – 1.0 (internal resolution multiplier)
};

// ---------------------------------------------------------------------------
// Quality preset factory functions
//
// Each function returns a fully-populated RenderSettings.  Start from a preset
// and override individual fields freely — presets are not an enum carried by
// the struct.
// ---------------------------------------------------------------------------

/// Lowest quality — fast on any hardware.
/// 512 directional shadow, 1 cascade, hard filter, SSAO off, bloom off.
RenderSettings renderSettingsLow();

/// Medium desktop quality.
/// 1024 directional shadow, 2 cascades, PCF4x4, SSAO off, bloom on.
RenderSettings renderSettingsMedium();

/// High desktop quality (platform default for capable desktops).
/// 2048 directional shadow, 3 cascades, PCF4x4, SSAO 16 samples, bloom on.
RenderSettings renderSettingsHigh();

/// Ultra desktop quality.
/// 4096 directional shadow, 4 cascades, PCF8x8, SSAO 32 samples, bloom on.
RenderSettings renderSettingsUltra();

/// Mobile-optimised preset (TBDR-aware).
/// 1024 directional shadow, 2 cascades, hard filter, SSAO off, no depth prepass.
RenderSettings renderSettingsMobile();

/// Selects the best preset for the detected GPU capabilities.
///
/// Logic:
///   - isTBDR   → start from renderSettingsMobile(), cap directionalRes ≤ 1024,
///                disable depthPrepassEnabled, disable SSAO
///   - otherwise → start from renderSettingsHigh()
///   - !computeShaders → disable SSAO (SSAO requires compute path)
RenderSettings renderSettingsPlatformDefault(const GpuFeatures& gpu);

}  // namespace engine::rendering
