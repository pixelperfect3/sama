#pragma once

#include <cstdint>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// GpuFeatures — hardware capability snapshot
//
// Queried once at startup from bgfx::getCaps() after bgfx::init().
// All rendering systems read this by const reference; nobody mutates it after
// startup.  In unit tests, construct directly with known values or use the
// convenience factories below.
// ---------------------------------------------------------------------------

struct GpuFeatures
{
    // -----------------------------------------------------------------------
    // Vertex attribute formats
    // -----------------------------------------------------------------------

    /// AttribType::Half supported — float16 UVs and normals.
    bool halfPrecisionAttribs = false;

    /// AttribType::Uint10 supported — RGB10A2 packed normals/tangents.
    bool uint10Attribs = false;

    // -----------------------------------------------------------------------
    // Texture compression formats
    // -----------------------------------------------------------------------

    /// BC1–BC7 family (desktop Windows/Linux).
    bool textureBC = false;

    /// ASTC LDR + HDR (Apple platforms, modern Android).
    bool textureASTC = false;

    /// ETC2 (Android fallback for devices without ASTC).
    bool textureETC2 = false;

    /// BGFX_CAPS_TEXTURE_COMPARE_LEQUAL — hardware PCF shadow sampling.
    bool textureShadowCompare = false;

    /// BC6H — HDR environment maps on desktop.
    bool textureBC6H = false;

    // -----------------------------------------------------------------------
    // GPU architecture
    // -----------------------------------------------------------------------

    /// Tile-Based Deferred Renderer: true for all mobile GPUs (Mali, Adreno,
    /// PowerVR) and Apple Silicon (M-series, A-series).
    /// Drives: depth prepass disabled, sort key priority front-to-back first,
    /// transient attachment hints, SSAO off by default.
    bool isTBDR = false;

    // -----------------------------------------------------------------------
    // Rendering features
    // -----------------------------------------------------------------------

    /// BGFX_CAPS_COMPUTE — required for GPU-driven culling and SSAO.
    bool computeShaders = false;

    /// BGFX_CAPS_DRAW_INDIRECT — GPU-driven instanced draw calls (future).
    bool indirectDraw = false;

    /// BGFX_CAPS_INSTANCING — hardware instancing.
    bool instancing = false;

    // -----------------------------------------------------------------------
    // Limits
    // -----------------------------------------------------------------------

    /// Caps shadow map and environment map allocations.
    uint32_t maxTextureSize = 2048;

    /// Hard per-frame draw-call budget reported by bgfx.
    uint32_t maxDrawCalls = 65535;

    // -----------------------------------------------------------------------
    // Preferred texture format (resolved at startup from the flags above)
    // -----------------------------------------------------------------------

    enum class TextureFormat : uint8_t
    {
        ASTC,
        BC,
        ETC2,
        Uncompressed
    };

    TextureFormat preferredTextureFormat = TextureFormat::Uncompressed;

    // -----------------------------------------------------------------------
    // Factories
    // -----------------------------------------------------------------------

    /// Populated from bgfx::getCaps() after bgfx::init().
    /// Returns desktopDefaults() as a stub until bgfx is wired in Phase 1.
    static GpuFeatures query();

    /// Sane defaults for a discrete desktop GPU (BC textures, compute, IMR).
    static GpuFeatures desktopDefaults();

    /// Sane defaults for a mobile GPU (ASTC, TBDR, no compute by default).
    static GpuFeatures mobileDefaults();
};

}  // namespace engine::rendering
