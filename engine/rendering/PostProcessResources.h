#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// PostProcessResources — owns all framebuffers and textures for the
// post-process chain.
//
// Call validate() at startup and on every window resize.  It is idempotent:
// if the requested dimensions and step count match the current allocation,
// it is a no-op.  shutdown() destroys every bgfx handle this object owns.
//
// HDR scene:
//   hdrColor_   RGBA16F  — scene color output from opaque/transparent passes
//   sceneDepth_ D24      — scene depth (packed with hdrColor_ in sceneFb_)
//
// Bloom chain (up to 5 levels):
//   bloomLevels_[0]   full resolution filtered output (threshold)
//   bloomLevels_[1]   half resolution
//   bloomLevels_[2]   quarter resolution
//   …
//   Each level has a dedicated single-texture framebuffer bloomFbs_[i].
//
// LDR target (input to FXAA):
//   ldrColor_   BGRA8   — tonemapped output
// ---------------------------------------------------------------------------

class PostProcessResources
{
public:
    PostProcessResources()
    {
        for (auto& h : bloomLevels_)
            h = BGFX_INVALID_HANDLE;
        for (auto& h : bloomFbs_)
            h = BGFX_INVALID_HANDLE;
    }

    // Creates / re-creates all framebuffers and textures.
    // Safe to call multiple times; handles old resources before re-allocating.
    void validate(uint16_t width, uint16_t height, uint8_t downsampleSteps);

    // Destroy every handle owned by this object.
    void shutdown();

    // HDR scene color (output of opaque / transparent passes).
    bgfx::TextureHandle hdrColor() const
    {
        return hdrColor_;
    }

    // Framebuffer that combines hdrColor_ + sceneDepth_.
    bgfx::FrameBufferHandle sceneFb() const
    {
        return sceneFb_;
    }

    // Bloom mip chain.
    bgfx::TextureHandle bloomLevel(uint32_t level) const;
    bgfx::FrameBufferHandle bloomLevelFb(uint32_t level) const;

    // Tonemapped LDR target (input to FXAA).
    bgfx::TextureHandle ldrColor() const
    {
        return ldrColor_;
    }

    bgfx::FrameBufferHandle ldrFb() const
    {
        return ldrFb_;
    }

    uint16_t width() const
    {
        return width_;
    }

    uint16_t height() const
    {
        return height_;
    }

    uint8_t steps() const
    {
        return steps_;
    }

private:
    static constexpr uint8_t kMaxSteps = 5;

    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint8_t steps_ = 0;

    bgfx::TextureHandle hdrColor_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sceneDepth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle sceneFb_ = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle bloomLevels_[kMaxSteps];
    bgfx::FrameBufferHandle bloomFbs_[kMaxSteps];

    bgfx::TextureHandle ldrColor_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle ldrFb_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::rendering
