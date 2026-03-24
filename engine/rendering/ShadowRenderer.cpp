#include "engine/rendering/ShadowRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

bool ShadowRenderer::init(const ShadowDesc& desc)
{
    desc_ = desc;

    // The Noop renderer (headless unit tests) cannot create real GPU resources.
    // Return true without allocating anything — all handles remain invalid.
    // shutdown() and shadowMatrix() are safe to call in this state.
    if (bgfx::getRendererType() == bgfx::RendererType::Noop)
        return true;

    const uint32_t res = desc_.resolution;

    // Create the shadow atlas texture with depth format.
    // BGFX_SAMPLER_COMPARE_LEQUAL enables hardware PCF comparison sampling
    // (shadow2D in shaderc macros) when the texture is sampled in the PBR pass.
    // BGFX_TEXTURE_RT marks it as a render target so it can be attached to a
    // framebuffer for the shadow depth pass.
    const uint64_t textureFlags =
        BGFX_TEXTURE_RT | static_cast<uint64_t>(BGFX_SAMPLER_COMPARE_LEQUAL);

    atlas_ = bgfx::createTexture2D(static_cast<uint16_t>(res), static_cast<uint16_t>(res), false, 1,
                                   bgfx::TextureFormat::D32F, textureFlags);

    if (!bgfx::isValid(atlas_))
        return false;

    // Create one framebuffer per cascade, each rendering into its tile of the atlas.
    for (uint32_t i = 0; i < desc_.cascadeCount && i < 4; ++i)
    {
        bgfx::Attachment at;
        at.init(atlas_, bgfx::Access::Write, 0, 0);  // layer=0, mip=0
        fb_[i] = bgfx::createFrameBuffer(1, &at, false);

        if (!bgfx::isValid(fb_[i]))
        {
            shutdown();
            return false;
        }
    }

    return true;
}

void ShadowRenderer::shutdown()
{
    for (auto& fb : fb_)
    {
        if (bgfx::isValid(fb))
        {
            bgfx::destroy(fb);
            fb = BGFX_INVALID_HANDLE;
        }
    }

    if (bgfx::isValid(atlas_))
    {
        bgfx::destroy(atlas_);
        atlas_ = BGFX_INVALID_HANDLE;
    }
}

void ShadowRenderer::beginCascade(uint32_t cascadeIdx, const math::Mat4& lightView,
                                  const math::Mat4& lightProj)
{
    const uint32_t i = cascadeIdx;
    const uint32_t res = desc_.resolution;
    const bgfx::ViewId viewId = static_cast<bgfx::ViewId>(kViewShadowBase + i);

    // Compute the atlas tile rectangle for this cascade.
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t w = static_cast<uint16_t>(res);
    uint16_t h = static_cast<uint16_t>(res);

    if (desc_.cascadeCount == 2)
    {
        // Two tiles side-by-side.
        w = static_cast<uint16_t>(res / 2);
        x = static_cast<uint16_t>(i * (res / 2));
    }
    else if (desc_.cascadeCount == 4)
    {
        // 2x2 grid.
        w = static_cast<uint16_t>(res / 2);
        h = static_cast<uint16_t>(res / 2);
        x = static_cast<uint16_t>((i % 2) * (res / 2));
        y = static_cast<uint16_t>((i / 2) * (res / 2));
    }

    bgfx::setViewFrameBuffer(viewId, fb_[i]);
    bgfx::setViewRect(viewId, x, y, w, h);
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(viewId, &lightView[0][0], &lightProj[0][0]);

    lightView_[i] = lightView;
    lightProj_[i] = lightProj;
}

math::Vec4 ShadowRenderer::cascadeUvRect(uint32_t cascadeIdx) const
{
    const uint32_t i = cascadeIdx;

    if (desc_.cascadeCount == 1)
    {
        return math::Vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }

    if (desc_.cascadeCount == 2)
    {
        const float x0 = static_cast<float>(i) * 0.5f;
        return math::Vec4(x0, 0.0f, x0 + 0.5f, 1.0f);
    }

    // 4 cascades — 2x2 grid.
    const float x0 = static_cast<float>(i % 2) * 0.5f;
    const float y0 = static_cast<float>(i / 2) * 0.5f;
    return math::Vec4(x0, y0, x0 + 0.5f, y0 + 0.5f);
}

math::Mat4 ShadowRenderer::shadowMatrix(uint32_t cascadeIdx) const
{
    const uint32_t i = cascadeIdx;

    // Bias matrix: NDC XY [-1,1] -> UV [0,1]; Z unchanged (GLM_FORCE_DEPTH_ZERO_TO_ONE
    // means depth is already in [0,1] — no scale/offset needed for Z).
    const math::Mat4 biasMatrix = glm::translate(math::Mat4(1.0f), math::Vec3(0.5f, 0.5f, 0.0f)) *
                                  glm::scale(math::Mat4(1.0f), math::Vec3(0.5f, 0.5f, 1.0f));

    // Atlas UV offset for cascade i: scale and translate the [0,1] UV into the
    // cascade tile's sub-rect within the atlas.
    const math::Vec4 uvRect = cascadeUvRect(i);
    const math::Mat4 atlasScale =
        glm::scale(math::Mat4(1.0f), math::Vec3(uvRect.z - uvRect.x, uvRect.w - uvRect.y, 1.0f));
    const math::Mat4 atlasTranslate =
        glm::translate(math::Mat4(1.0f), math::Vec3(uvRect.x, uvRect.y, 0.0f));

    return atlasTranslate * atlasScale * biasMatrix * lightProj_[i] * lightView_[i];
}

}  // namespace engine::rendering
