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

void SkyboxRenderer::init()
{
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();

    vbh_ = bgfx::createVertexBuffer(bgfx::makeRef(kFullscreenTri, sizeof(kFullscreenTri)), layout);
    s_skybox_ = bgfx::createUniform("s_skybox", bgfx::UniformType::Sampler);
    // ShaderLoader returns engine::rendering::ProgramHandle (bgfx-free
    // wrapper); widen to bgfx for the engine-internal storage member.
    program_ = bgfx::ProgramHandle{loadSkyboxProgram().idx};
}

void SkyboxRenderer::shutdown()
{
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(s_skybox_))
    {
        bgfx::destroy(s_skybox_);
        s_skybox_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(vbh_))
    {
        bgfx::destroy(vbh_);
        vbh_ = BGFX_INVALID_HANDLE;
    }
}

void SkyboxRenderer::render(bgfx::ViewId viewId, bgfx::TextureHandle cubemap)
{
    if (!bgfx::isValid(program_) || !bgfx::isValid(cubemap))
        return;

    // Single fullscreen triangle. Depth test = LESS_EQUAL with depth write
    // disabled so the skybox sits at the far plane (gl_Position.z=w in the
    // VS) without polluting the depth buffer. No culling needed since the
    // triangle is in clip space and always front-facing.
    const uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA;

    bgfx::setTexture(0, s_skybox_, cubemap);
    bgfx::setVertexBuffer(0, vbh_);
    bgfx::setState(state);
    bgfx::submit(viewId, program_);
}

}  // namespace engine::rendering
