#include "engine/rendering/GpuFeatures.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// GpuFeatures::query
//
// TODO (Phase 1 — renderer init): replace this stub with a real implementation
// that calls bgfx::getCaps() after bgfx::init() has returned successfully.
//
// Example wiring (do not add bgfx include here yet — keep this header clean):
//
//   const bgfx::Caps* caps = bgfx::getCaps();
//   GpuFeatures f;
//   f.halfPrecisionAttribs  = (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_HALF)   != 0;
//   f.uint10Attribs         = (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_UINT10) != 0;
//   f.textureBC             = (caps->formats[bgfx::TextureFormat::BC1] & ...) != 0;
//   ...
// ---------------------------------------------------------------------------

GpuFeatures GpuFeatures::query()
{
    // Stub: returns desktopDefaults() until bgfx is wired up in Phase 1.
    return desktopDefaults();
}

// ---------------------------------------------------------------------------
// GpuFeatures::desktopDefaults
//
// Represents a typical discrete desktop GPU (e.g. Nvidia/AMD on Windows or a
// Mac with a discrete GPU).  BC texture family, compute shaders, IMR arch.
// ---------------------------------------------------------------------------

GpuFeatures GpuFeatures::desktopDefaults()
{
    GpuFeatures f;
    f.halfPrecisionAttribs = true;
    f.uint10Attribs = true;
    f.textureBC = true;
    f.textureASTC = false;
    f.textureETC2 = false;
    f.textureShadowCompare = true;
    f.textureBC6H = true;
    f.isTBDR = false;
    f.computeShaders = true;
    f.indirectDraw = true;
    f.instancing = true;
    f.maxTextureSize = 16384;
    f.maxDrawCalls = 65535;
    f.preferredTextureFormat = GpuFeatures::TextureFormat::BC;
    return f;
}

// ---------------------------------------------------------------------------
// GpuFeatures::mobileDefaults
//
// Represents a modern mobile GPU (e.g. ARM Mali, Qualcomm Adreno, or Apple
// A-series / M-series).  ASTC textures, TBDR architecture.
// Compute shaders are present on modern mobile but disabled here by default
// to give a conservative baseline; the real query() will fill the truth.
// ---------------------------------------------------------------------------

GpuFeatures GpuFeatures::mobileDefaults()
{
    GpuFeatures f;
    f.halfPrecisionAttribs = true;
    f.uint10Attribs = false;
    f.textureBC = false;
    f.textureASTC = true;
    f.textureETC2 = true;  // ETC2 is baseline for Android fallback
    f.textureShadowCompare = true;
    f.textureBC6H = false;
    f.isTBDR = true;
    f.computeShaders = false;
    f.indirectDraw = false;
    f.instancing = true;
    f.maxTextureSize = 4096;
    f.maxDrawCalls = 16384;
    f.preferredTextureFormat = GpuFeatures::TextureFormat::ASTC;
    return f;
}

}  // namespace engine::rendering
