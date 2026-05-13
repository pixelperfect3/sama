#include "engine/rendering/PostProcessResources.h"

#include <bgfx/bgfx.h>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

void safeDestroyTexture(bgfx::TextureHandle& h)
{
    if (bgfx::isValid(h))
    {
        bgfx::destroy(h);
        h = BGFX_INVALID_HANDLE;
    }
}

void safeDestroyFrameBuffer(bgfx::FrameBufferHandle& h)
{
    if (bgfx::isValid(h))
    {
        bgfx::destroy(h);
        h = BGFX_INVALID_HANDLE;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

void PostProcessResources::validate(uint16_t width, uint16_t height, uint8_t downsampleSteps)
{
    // In headless (Noop) mode bgfx silently accepts but does nothing real.
    // We still allocate handles so the rest of the pipeline can query them
    // without branching everywhere.

    if (width == width_ && height == height_ && downsampleSteps == steps_)
        return;

    // Release old resources first.
    shutdown();

    width_ = width;
    height_ = height;
    steps_ = downsampleSteps < kMaxSteps ? downsampleSteps : kMaxSteps;

    // -----------------------------------------------------------------
    // HDR scene color + depth → sceneFb_
    // -----------------------------------------------------------------
    hdrColor_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::RGBA16F,
                                      BGFX_TEXTURE_RT);

    // D24S8 (depth24 + stencil8) packed format.  D24 alone would suffice for
    // the runtime engine, but the editor's selection-outline pass needs an
    // 8-bit stencil buffer alongside scene depth.  Choosing D24S8 here over
    // a separate stencil attachment keeps the runtime cost identical (same
    // 32-bit-per-pixel target) and avoids a second render-target binding.
    // Sampling the depth component still works on every backend bgfx supports.
    sceneDepth_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::D24S8,
                                        BGFX_TEXTURE_RT);

    {
        bgfx::TextureHandle attachments[2] = {hdrColor_, sceneDepth_};
        sceneFb_ = bgfx::createFrameBuffer(2, attachments, false);
    }

    // -----------------------------------------------------------------
    // Bloom mip chain — each level is half the previous resolution.
    // Level 0 is full resolution (the threshold pass writes here).
    // -----------------------------------------------------------------
    for (uint8_t i = 0; i < steps_; ++i)
    {
        uint16_t lw = width_ >> i;
        uint16_t lh = height_ >> i;
        if (lw < 1)
            lw = 1;
        if (lh < 1)
            lh = 1;

        bloomLevels_[i] =
            bgfx::createTexture2D(lw, lh, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT);

        bloomFbs_[i] = bgfx::createFrameBuffer(1, &bloomLevels_[i], false);
    }

    // -----------------------------------------------------------------
    // LDR target (tonemapper output, input to FXAA)
    // -----------------------------------------------------------------
    ldrColor_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::BGRA8,
                                      BGFX_TEXTURE_RT);

    ldrFb_ = bgfx::createFrameBuffer(1, &ldrColor_, false);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void PostProcessResources::shutdown()
{
    safeDestroyFrameBuffer(ldrFb_);
    safeDestroyTexture(ldrColor_);

    for (uint8_t i = 0; i < kMaxSteps; ++i)
    {
        safeDestroyFrameBuffer(bloomFbs_[i]);
        safeDestroyTexture(bloomLevels_[i]);
    }

    safeDestroyFrameBuffer(sceneFb_);
    safeDestroyTexture(sceneDepth_);
    safeDestroyTexture(hdrColor_);

    width_ = 0;
    height_ = 0;
    steps_ = 0;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bgfx::TextureHandle PostProcessResources::bloomLevel(uint32_t level) const
{
    if (level >= steps_)
        return BGFX_INVALID_HANDLE;
    return bloomLevels_[level];
}

bgfx::FrameBufferHandle PostProcessResources::bloomLevelFb(uint32_t level) const
{
    if (level >= steps_)
        return BGFX_INVALID_HANDLE;
    return bloomFbs_[level];
}

}  // namespace engine::rendering
