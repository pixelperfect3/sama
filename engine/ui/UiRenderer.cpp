#include "engine/ui/UiRenderer.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/SpriteBatcher.h"
#include "engine/ui/DefaultFont.h"
#include "engine/ui/GlyphMetrics.h"
#include "engine/ui/IFont.h"
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
// Inline UTF-8 decoder. Returns 0 on malformed input and advances past the
// offending byte so we always make forward progress.
// ---------------------------------------------------------------------------

static uint32_t utf8Next(const char** p)
{
    const unsigned char* s = reinterpret_cast<const unsigned char*>(*p);
    if (!*s)
        return 0;
    uint32_t cp = 0;
    int extra = 0;
    if (*s < 0x80)
    {
        cp = *s;
        extra = 0;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
        cp = *s & 0x1F;
        extra = 1;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
        cp = *s & 0x0F;
        extra = 2;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
        cp = *s & 0x07;
        extra = 3;
    }
    else
    {
        ++(*p);
        return 0;
    }
    ++s;
    for (int i = 0; i < extra; ++i)
    {
        if ((*s & 0xC0) != 0x80)
        {
            *p = reinterpret_cast<const char*>(s);
            return 0;
        }
        cp = (cp << 6) | (*s & 0x3F);
        ++s;
    }
    *p = reinterpret_cast<const char*>(s);
    return cp;
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
// Text pass helper — submit one batch of text commands that share the same
// (program, atlas) pair. Builds one transient vertex/index buffer and
// submits a single draw call. `totalGlyphs` must be the precomputed glyph
// count across all commands in [begin, end).
// ---------------------------------------------------------------------------

namespace
{

uint32_t countGlyphs(const char* text)
{
    if (!text)
        return 0;
    uint32_t n = 0;
    const char* p = text;
    while (*p)
    {
        const uint32_t cp = utf8Next(&p);
        if (cp == 0 || cp == '\n')
            continue;
        ++n;
    }
    return n;
}

}  // namespace

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

    const uint64_t blendState =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    // =======================================================================
    // Pass 1: Rect + TexturedRect quads.
    // =======================================================================

    uint32_t quadCount = 0;
    for (const auto& cmd : cmds)
    {
        if (cmd.type != UiDrawCmd::Text)
            ++quadCount;
    }

    if (quadCount > 0)
    {
        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};

        if (bgfx::getAvailTransientVertexBuffer(quadCount * 4, layout_) &&
            bgfx::getAvailTransientIndexBuffer(quadCount * 6))
        {
            bgfx::allocTransientVertexBuffer(&tvb, quadCount * 4, layout_);
            bgfx::allocTransientIndexBuffer(&tib, quadCount * 6);

            auto* verts = reinterpret_cast<UiVertex*>(tvb.data);
            auto* idx = reinterpret_cast<uint16_t*>(tib.data);

            uint32_t qi = 0;
            for (const auto& cmd : cmds)
            {
                if (cmd.type == UiDrawCmd::Text)
                    continue;

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

                verts[base + 0] = {x0, y0, u0, v0, rgba};
                verts[base + 1] = {x1, y0, u1, v0, rgba};
                verts[base + 2] = {x1, y1, u1, v1, rgba};
                verts[base + 3] = {x0, y1, u0, v1, rgba};

                const auto vi = static_cast<uint16_t>(base);
                idx[qi * 6 + 0] = vi + 0;
                idx[qi * 6 + 1] = vi + 1;
                idx[qi * 6 + 2] = vi + 2;
                idx[qi * 6 + 3] = vi + 0;
                idx[qi * 6 + 4] = vi + 2;
                idx[qi * 6 + 5] = vi + 3;

                ++qi;
            }

            bgfx::setTexture(0, s_texture_, whiteTex_);
            bgfx::setState(blendState);
            bgfx::setVertexBuffer(0, &tvb);
            bgfx::setIndexBuffer(&tib);
            bgfx::submit(viewId, program_);
        }
    }

    // =======================================================================
    // Pass 2: Text commands. Group consecutive Text cmds by (program, atlas)
    // and submit one draw call per group. A group starts at index `i` and
    // extends through commands sharing the same font resources.
    // =======================================================================

    const std::size_t n = cmds.size();
    std::size_t i = 0;
    while (i < n)
    {
        if (cmds[i].type != UiDrawCmd::Text)
        {
            ++i;
            continue;
        }

        const IFont* font = cmds[i].font ? cmds[i].font : defaultFont();
        if (!font || !bgfx::isValid(font->program()) || !bgfx::isValid(font->atlasTexture()))
        {
            ++i;
            continue;
        }

        const bgfx::ProgramHandle prog = font->program();
        const bgfx::TextureHandle atlas = font->atlasTexture();

        // Collect a contiguous run of Text commands that can share the same
        // batch. Non-Text commands inside the run break the run.
        std::size_t j = i;
        uint32_t glyphs = 0;
        while (j < n && cmds[j].type == UiDrawCmd::Text)
        {
            const IFont* f = cmds[j].font ? cmds[j].font : defaultFont();
            if (!f || f->program().idx != prog.idx || f->atlasTexture().idx != atlas.idx)
                break;
            glyphs += countGlyphs(cmds[j].text);
            ++j;
        }

        if (glyphs == 0)
        {
            i = j;
            continue;
        }

        // Cap at 16-bit index budget: 4 verts/glyph -> max 16384 glyphs/batch.
        if (glyphs > 16000)
            glyphs = 16000;

        if (!bgfx::getAvailTransientVertexBuffer(glyphs * 4, layout_) ||
            !bgfx::getAvailTransientIndexBuffer(glyphs * 6))
        {
            i = j;
            continue;
        }

        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};
        bgfx::allocTransientVertexBuffer(&tvb, glyphs * 4, layout_);
        bgfx::allocTransientIndexBuffer(&tib, glyphs * 6);

        auto* verts = reinterpret_cast<UiVertex*>(tvb.data);
        auto* idx = reinterpret_cast<uint16_t*>(tib.data);

        uint32_t emitted = 0;
        for (std::size_t k = i; k < j && emitted < glyphs; ++k)
        {
            const auto& cmd = cmds[k];
            if (!cmd.text)
                continue;

            const IFont* f = cmd.font ? cmd.font : defaultFont();
            const float scale = cmd.fontSize / f->nominalSize();
            const uint32_t rgba = packColor(cmd.color);

            float cursorX = cmd.position.x;
            const float baselineY = cmd.position.y;
            uint32_t prev = 0;

            const char* p = cmd.text;
            while (*p && emitted < glyphs)
            {
                const uint32_t cp = utf8Next(&p);
                if (cp == 0)
                    continue;
                if (cp == '\n')
                {
                    cursorX = cmd.position.x;
                    prev = 0;
                    continue;
                }
                const GlyphMetrics* g = f->getGlyph(cp);
                if (!g)
                    continue;
                if (prev)
                    cursorX += f->getKerning(prev, cp) * scale;

                const float x0 = cursorX + g->offset.x * scale;
                const float y0 = baselineY + g->offset.y * scale;
                const float x1 = x0 + g->size.x * scale;
                const float y1 = y0 + g->size.y * scale;

                if (g->size.x > 0.f && g->size.y > 0.f)
                {
                    const uint32_t base = emitted * 4;
                    verts[base + 0] = {x0, y0, g->uvRect.x, g->uvRect.y, rgba};
                    verts[base + 1] = {x1, y0, g->uvRect.z, g->uvRect.y, rgba};
                    verts[base + 2] = {x1, y1, g->uvRect.z, g->uvRect.w, rgba};
                    verts[base + 3] = {x0, y1, g->uvRect.x, g->uvRect.w, rgba};

                    const auto vi = static_cast<uint16_t>(base);
                    idx[emitted * 6 + 0] = vi + 0;
                    idx[emitted * 6 + 1] = vi + 1;
                    idx[emitted * 6 + 2] = vi + 2;
                    idx[emitted * 6 + 3] = vi + 0;
                    idx[emitted * 6 + 4] = vi + 2;
                    idx[emitted * 6 + 5] = vi + 3;
                    ++emitted;
                }

                cursorX += g->advance * scale;
                prev = cp;
            }
        }

        if (emitted > 0)
        {
            bgfx::setTexture(0, s_texture_, atlas);
            bgfx::setState(blendState);
            bgfx::setVertexBuffer(0, &tvb, 0, emitted * 4);
            bgfx::setIndexBuffer(&tib, 0, emitted * 6);
            font->bindResources();
            bgfx::submit(viewId, prog);
        }

        i = j;
    }
}

}  // namespace engine::ui
