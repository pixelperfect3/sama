#include "engine/rendering/SkyboxRenderer.h"

#include <bgfx/bgfx.h>

#include "engine/rendering/ShaderLoader.h"

namespace engine::rendering
{

namespace
{

// Three vertices forming a single oversized triangle in clip space that
// covers the entire viewport with no overdraw — the canonical fullscreen
// pass primitive. NDC coordinates extend beyond [-1, 1] so the triangle
// fully encloses the screen rectangle after rasterization clipping.
struct SkyboxVertex
{
    float x, y;
};

const SkyboxVertex kFullscreenTri[] = {
    {-1.f, -1.f},
    {3.f, -1.f},
    {-1.f, 3.f},
};

}  // namespace

// ---------------------------------------------------------------------------
// Impl — owns every bgfx-typed member so the public SkyboxRenderer header
// can stay bgfx-free.  Same pImpl pattern as UiRenderer.
// ---------------------------------------------------------------------------

struct SkyboxRenderer::Impl
{
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_skybox = BGFX_INVALID_HANDLE;
};

SkyboxRenderer::SkyboxRenderer() : impl_(std::make_unique<Impl>()) {}
SkyboxRenderer::~SkyboxRenderer() = default;
SkyboxRenderer::SkyboxRenderer(SkyboxRenderer&&) noexcept = default;
SkyboxRenderer& SkyboxRenderer::operator=(SkyboxRenderer&&) noexcept = default;

void SkyboxRenderer::init()
{
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();

    impl_->vbh =
        bgfx::createVertexBuffer(bgfx::makeRef(kFullscreenTri, sizeof(kFullscreenTri)), layout);
    impl_->s_skybox = bgfx::createUniform("s_skybox", bgfx::UniformType::Sampler);
    // ShaderLoader returns engine::rendering::ProgramHandle (bgfx-free
    // wrapper); widen to bgfx for the engine-internal storage member.
    impl_->program = bgfx::ProgramHandle{loadSkyboxProgram().idx};
}

void SkyboxRenderer::shutdown()
{
    if (bgfx::isValid(impl_->program))
    {
        bgfx::destroy(impl_->program);
        impl_->program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->s_skybox))
    {
        bgfx::destroy(impl_->s_skybox);
        impl_->s_skybox = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(impl_->vbh))
    {
        bgfx::destroy(impl_->vbh);
        impl_->vbh = BGFX_INVALID_HANDLE;
    }
}

void SkyboxRenderer::render(ViewId viewId, TextureHandle cubemap)
{
    // Boundary conversion — the wrapped TextureHandle is layout-identical
    // to bgfx::TextureHandle (RenderPass.cpp asserts this).
    const bgfx::TextureHandle bgfxCube{cubemap.idx};
    if (!bgfx::isValid(impl_->program) || !bgfx::isValid(bgfxCube))
        return;

    // Single fullscreen triangle. Depth test = LESS_EQUAL with depth write
    // disabled so the skybox sits at the far plane (gl_Position.z=w in the
    // VS) without polluting the depth buffer. No culling needed since the
    // triangle is in clip space and always front-facing.
    const uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA;

    bgfx::setTexture(0, impl_->s_skybox, bgfxCube);
    bgfx::setVertexBuffer(0, impl_->vbh);
    bgfx::setState(state);
    bgfx::submit(viewId, impl_->program);
}

bool SkyboxRenderer::isValid() const noexcept
{
    return bgfx::isValid(impl_->program);
}

}  // namespace engine::rendering
