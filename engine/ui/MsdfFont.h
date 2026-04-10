#pragma once

#include <ankerl/unordered_dense.h>
#include <bgfx/bgfx.h>

#include <cstdint>

#include "engine/ui/GlyphMetrics.h"
#include "engine/ui/IFont.h"

namespace engine::ui
{

// ---------------------------------------------------------------------------
// MsdfFont
//
// Multi-channel signed distance field font backend. Same per-glyph quad
// rendering model as BitmapFont, but the atlas pixels encode a 3-channel
// signed distance field (via msdf-atlas-gen) rather than coverage.
// The fragment shader reconstructs sharp edges at any scale by taking the
// median of the three channels and applying smoothstep.
//
// Load from a JSON metrics file + RGB atlas PNG. The JSON must follow the
// schema produced by `msdf-atlas-gen --json` (see loadFromFile() for the
// expected fields).
// ---------------------------------------------------------------------------

class MsdfFont final : public IFont
{
public:
    MsdfFont() = default;
    ~MsdfFont() override
    {
        shutdown();
    }

    MsdfFont(const MsdfFont&) = delete;
    MsdfFont& operator=(const MsdfFont&) = delete;
    MsdfFont(MsdfFont&&) = delete;
    MsdfFont& operator=(MsdfFont&&) = delete;

    // Load the font from a metrics JSON file and an atlas PNG on disk.
    // Returns false if either file cannot be opened, the JSON is malformed,
    // or the atlas image cannot be decoded. Leaves the font in a
    // default-constructed state on failure (safe to re-try).
    bool loadFromFile(const char* metricsPath, const char* atlasPath);

    // Release GPU resources (atlas texture, program, uniform). Safe to call
    // multiple times or when load never succeeded.
    void shutdown();

    // IFont interface -------------------------------------------------------

    FontRenderer renderer() const noexcept override
    {
        return FontRenderer::Msdf;
    }

    const GlyphMetrics* getGlyph(uint32_t codepoint) const override;
    float getKerning(uint32_t left, uint32_t right) const override;

    float lineHeight() const noexcept override
    {
        return lineHeight_;
    }

    float nominalSize() const noexcept override
    {
        return nominalSize_;
    }

    bgfx::TextureHandle atlasTexture() const noexcept override
    {
        return atlas_;
    }

    bgfx::ProgramHandle program() const noexcept override
    {
        return program_;
    }

    // Binds u_msdfParams (pixelRange = distanceRange) before submission.
    // The atlas texture itself is already bound by UiRenderer through the
    // shared s_texture sampler at slot 0.
    void bindResources() const override;

    // Extra accessors (non-virtual) ----------------------------------------

    float distanceRange() const noexcept
    {
        return distanceRange_;
    }

    std::size_t glyphCount() const noexcept
    {
        return glyphs_.size();
    }

private:
    ankerl::unordered_dense::map<uint32_t, GlyphMetrics> glyphs_;
    ankerl::unordered_dense::map<uint64_t, float> kerning_;

    float lineHeight_ = 0.f;
    float nominalSize_ = 0.f;
    float distanceRange_ = 4.f;  // pixels — baked into the atlas by msdf-atlas-gen

    bgfx::TextureHandle atlas_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_msdfParams_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::ui
