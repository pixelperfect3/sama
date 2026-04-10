#include "engine/ui/UiRenderer.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/SpriteBatcher.h"
#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

// ---------------------------------------------------------------------------
// Vertex — matches spriteLayout() from SpriteBatcher.
// ---------------------------------------------------------------------------

struct UiVertex
{
    float x, y;      // 2D position (screen pixels)
    float u, v;      // texture coordinates
    uint32_t color;  // ABGR packed
};

// ---------------------------------------------------------------------------
// Helper: pack RGBA [0,1] float4 into uint32_t ABGR (bgfx Color0 convention).
// ---------------------------------------------------------------------------

static uint32_t packColor(const glm::vec4& c)
{
    const auto r = static_cast<uint32_t>(glm::clamp(c.x, 0.f, 1.f) * 255.f + 0.5f);
    const auto g = static_cast<uint32_t>(glm::clamp(c.y, 0.f, 1.f) * 255.f + 0.5f);
    const auto b = static_cast<uint32_t>(glm::clamp(c.z, 0.f, 1.f) * 255.f + 0.5f);
    const auto a = static_cast<uint32_t>(glm::clamp(c.w, 0.f, 1.f) * 255.f + 0.5f);
    return (a << 24u) | (b << 16u) | (g << 8u) | r;
}

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

void UiRenderer::init()
{
    program_ = engine::rendering::loadSpriteProgram();
    layout_ = engine::rendering::spriteLayout();

    s_texture_ = bgfx::createUniform("s_texture", bgfx::UniformType::Sampler);

    // Create a 1x1 white texture for solid-color rects.
    const uint32_t white = 0xFFFFFFFF;
    whiteTex_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                      BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT,
                                      bgfx::copy(&white, sizeof(white)));
}

void UiRenderer::shutdown()
{
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(s_texture_))
    {
        bgfx::destroy(s_texture_);
        s_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(whiteTex_))
    {
        bgfx::destroy(whiteTex_);
        whiteTex_ = BGFX_INVALID_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void UiRenderer::render(const UiDrawList& drawList, bgfx::ViewId viewId, uint16_t screenW,
                        uint16_t screenH)
{
    const auto& cmds = drawList.commands();
    if (cmds.empty())
        return;

    // Headless (Noop) renderer — transient buffers are unavailable.
    if (bgfx::getRendererType() == bgfx::RendererType::Noop)
        return;

    // Set up orthographic projection: (0,0) top-left, (screenW, screenH)
    // bottom-right.  Uses glm::ortho to match the convention in UiRenderSystem.
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    const glm::mat4 ortho = glm::ortho(0.f, w, h, 0.f, -1.f, 1.f);
    bgfx::setViewTransform(viewId, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(viewId, 0, 0, screenW, screenH);

    // Count quads (skip Text commands).
    uint32_t quadCount = 0;
    for (const auto& cmd : cmds)
    {
        if (cmd.type != UiDrawCmd::Text)
            ++quadCount;
    }

    if (quadCount == 0)
        return;

    // Allocate transient buffers for all quads.
    bgfx::TransientVertexBuffer tvb{};
    bgfx::TransientIndexBuffer tib{};

    if (!bgfx::getAvailTransientVertexBuffer(quadCount * 4, layout_))
        return;
    if (!bgfx::getAvailTransientIndexBuffer(quadCount * 6))
        return;

    bgfx::allocTransientVertexBuffer(&tvb, quadCount * 4, layout_);
    bgfx::allocTransientIndexBuffer(&tib, quadCount * 6);

    auto* verts = reinterpret_cast<UiVertex*>(tvb.data);
    auto* idx = reinterpret_cast<uint16_t*>(tib.data);

    // Fill vertex/index data.
    uint32_t qi = 0;
    for (const auto& cmd : cmds)
    {
        if (cmd.type == UiDrawCmd::Text)
            continue;  // Phase 4

        const float x0 = cmd.position.x;
        const float y0 = cmd.position.y;
        const float x1 = x0 + cmd.size.x;
        const float y1 = y0 + cmd.size.y;

        float u0, v0, u1, v1;
        if (cmd.type == UiDrawCmd::TexturedRect)
        {
            u0 = cmd.uvRect.x;
            v0 = cmd.uvRect.y;
            u1 = cmd.uvRect.z;
            v1 = cmd.uvRect.w;
        }
        else
        {
            u0 = 0.f;
            v0 = 0.f;
            u1 = 1.f;
            v1 = 1.f;
        }

        const uint32_t rgba = packColor(cmd.color);
        const uint32_t base = qi * 4;

        // Four corners: top-left, top-right, bottom-right, bottom-left.
        verts[base + 0] = {x0, y0, u0, v0, rgba};
        verts[base + 1] = {x1, y0, u1, v0, rgba};
        verts[base + 2] = {x1, y1, u1, v1, rgba};
        verts[base + 3] = {x0, y1, u0, v1, rgba};

        // Two triangles: 0-1-2, 0-2-3.
        const auto v = static_cast<uint16_t>(base);
        idx[qi * 6 + 0] = v + 0;
        idx[qi * 6 + 1] = v + 1;
        idx[qi * 6 + 2] = v + 2;
        idx[qi * 6 + 3] = v + 0;
        idx[qi * 6 + 4] = v + 2;
        idx[qi * 6 + 5] = v + 3;

        ++qi;
    }

    // Submit all quads in a single draw call with the white texture.
    // For now, all commands (Rect + TexturedRect) share a single batch.
    // TexturedRect per-command textures will need per-batch splitting in a
    // future iteration; the current Phase 3 spec only tests solid Rects.
    bgfx::setTexture(0, s_texture_, whiteTex_);

    const uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    bgfx::setState(state);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::submit(viewId, program_);
}

}  // namespace engine::ui
