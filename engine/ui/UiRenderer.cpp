#include "engine/ui/UiRenderer.h"

#include <bgfx/bgfx.h>

#include <algorithm>
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
#include "engine/ui/SlugFont.h"
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

// Vertex used by the rounded-rect program. Same first 20 bytes as UiVertex
// (so the rect emission code can be shared between sharp + rounded rects),
// plus a vec4 carrying (halfWidth, halfHeight, cornerRadius, _pad) — same
// value for all 4 vertices of one rect.
struct UiRoundedVertex
{
    float x, y;
    float u, v;
    uint32_t color;
    float halfW, halfH, radius, _pad;
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

    // Rounded-rect path: own program + own vertex layout. The layout is
    // the sprite layout plus an extra TEXCOORD1 vec4 carrying (halfW,
    // halfH, cornerRadius, _pad). All 4 vertices of one rect share the
    // same value.
    roundedProgram_ = engine::rendering::loadRoundedRectProgram();
    roundedLayout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .end();

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
    if (bgfx::isValid(roundedProgram_))
    {
        bgfx::destroy(roundedProgram_);
        roundedProgram_ = BGFX_INVALID_HANDLE;
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

    // Force sequential draw order on this view. bgfx's default sort mode
    // (Default == DepthAscending) reorders draw calls within a view by
    // depth, which is fine for opaque 3D geometry but breaks UI: a
    // UiButton submits its background rect first (rect pass) and then its
    // label as a text command (text pass), but with depth sorting bgfx
    // can flip them and draw the rect ON TOP of the text — making
    // labels invisible. Sequential mode preserves submission order, so
    // each widget's background draws first and its text draws last.
    //
    // Reported by a downstream game integration; the fix lives here so
    // every UiRenderer caller gets it for free without having to know
    // about the bgfx view mode default.
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(viewId, 0, 0, screenW, screenH);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::touch(viewId);

    // Set up orthographic projection: (0,0) top-left, (screenW, screenH)
    // bottom-right.  Uses glm::ortho to match the convention in UiRenderSystem.
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    const glm::mat4 ortho = glm::ortho(0.f, w, h, 0.f, -1.f, 1.f);
    bgfx::setViewTransform(viewId, nullptr, glm::value_ptr(ortho));

    const uint64_t blendState =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    // =======================================================================
    // Pass 1a: Sharp Rect + TexturedRect quads (cornerRadius == 0).
    // Submitted with the existing sprite program + sprite vertex layout.
    // =======================================================================

    uint32_t sharpCount = 0;
    uint32_t roundedCount = 0;
    for (const auto& cmd : cmds)
    {
        if (cmd.type == UiDrawCmd::Text)
            continue;
        if (cmd.cornerRadius > 0.f && cmd.type == UiDrawCmd::Rect)
            ++roundedCount;
        else
            ++sharpCount;
    }

    if (sharpCount > 0)
    {
        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};

        if (bgfx::getAvailTransientVertexBuffer(sharpCount * 4, layout_) &&
            bgfx::getAvailTransientIndexBuffer(sharpCount * 6))
        {
            bgfx::allocTransientVertexBuffer(&tvb, sharpCount * 4, layout_);
            bgfx::allocTransientIndexBuffer(&tib, sharpCount * 6);

            auto* verts = reinterpret_cast<UiVertex*>(tvb.data);
            auto* idx = reinterpret_cast<uint16_t*>(tib.data);

            uint32_t qi = 0;
            for (const auto& cmd : cmds)
            {
                if (cmd.type == UiDrawCmd::Text)
                    continue;
                // Skip rounded Rect commands — they go through Pass 1b.
                if (cmd.cornerRadius > 0.f && cmd.type == UiDrawCmd::Rect)
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
    // Pass 1b: Rounded Rect quads (cornerRadius > 0). Same vertex format
    // as sharp rects + 4 extra floats per vertex carrying (halfW, halfH,
    // cornerRadius, _pad). Fragment shader runs the rounded-box SDF and
    // uses fwidth() to derive an antialiased coverage mask.
    // =======================================================================

    if (roundedCount > 0 && bgfx::isValid(roundedProgram_))
    {
        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};

        if (bgfx::getAvailTransientVertexBuffer(roundedCount * 4, roundedLayout_) &&
            bgfx::getAvailTransientIndexBuffer(roundedCount * 6))
        {
            bgfx::allocTransientVertexBuffer(&tvb, roundedCount * 4, roundedLayout_);
            bgfx::allocTransientIndexBuffer(&tib, roundedCount * 6);

            auto* verts = reinterpret_cast<UiRoundedVertex*>(tvb.data);
            auto* idx = reinterpret_cast<uint16_t*>(tib.data);

            uint32_t qi = 0;
            for (const auto& cmd : cmds)
            {
                if (cmd.type != UiDrawCmd::Rect || cmd.cornerRadius <= 0.f)
                    continue;

                const float x0 = cmd.position.x;
                const float y0 = cmd.position.y;
                const float x1 = x0 + cmd.size.x;
                const float y1 = y0 + cmd.size.y;
                const float halfW = cmd.size.x * 0.5f;
                const float halfH = cmd.size.y * 0.5f;
                // Clamp the radius so it can't exceed the smallest half-dimension
                // (otherwise the SDF produces visual garbage at the corners).
                const float r = std::min({cmd.cornerRadius, halfW, halfH});

                const uint32_t rgba = packColor(cmd.color);
                const uint32_t base = qi * 4;

                verts[base + 0] = {x0, y0, 0.f, 0.f, rgba, halfW, halfH, r, 0.f};
                verts[base + 1] = {x1, y0, 1.f, 0.f, rgba, halfW, halfH, r, 0.f};
                verts[base + 2] = {x1, y1, 1.f, 1.f, rgba, halfW, halfH, r, 0.f};
                verts[base + 3] = {x0, y1, 0.f, 1.f, rgba, halfW, halfH, r, 0.f};

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
            bgfx::submit(viewId, roundedProgram_);
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
        if (!font || !bgfx::isValid(font->program()))
        {
            ++i;
            continue;
        }

        // Slug fonts take a different submission path: one draw per glyph
        // because the per-glyph (curveOffset, curveCount) must be set as a
        // uniform. The atlas-based check below would also reject them since
        // SlugFont returns BGFX_INVALID_HANDLE for atlasTexture().
        if (font->renderer() == FontRenderer::Slug)
        {
            const auto* slug = static_cast<const SlugFont*>(font);
            const bgfx::ProgramHandle slugProg = font->program();
            // Walk THIS Text command (and any consecutive Text commands
            // sharing the same font program) emitting one draw per glyph.
            std::size_t k = i;
            while (k < n && cmds[k].type == UiDrawCmd::Text)
            {
                const IFont* f = cmds[k].font ? cmds[k].font : defaultFont();
                if (!f || f->program().idx != slugProg.idx)
                    break;
                renderSlugText(cmds[k], slug, slugProg, viewId, blendState);
                ++k;
            }
            i = k;
            continue;
        }

        if (!bgfx::isValid(font->atlasTexture()))
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

// ---------------------------------------------------------------------------
// renderSlugText — single text command, one draw per glyph.
//
// SlugFont stores per-glyph (curveOffset, curveCount) which the slug fragment
// shader reads from u_slugParams. There is no atlas — the shader instead
// fetches Bezier control points from the SlugFont's curve buffer texture and
// determines pixel coverage by ray-casting in glyph-local font space.
//
// To make the math work we write the glyph's font-space bounding-box corners
// into the per-vertex TEXCOORD0 (instead of an atlas UV). The slug shader
// reads v_texcoord0 as a glyph-local coordinate.
// ---------------------------------------------------------------------------

void UiRenderer::renderSlugText(const UiDrawCmd& cmd, const SlugFont* font,
                                bgfx::ProgramHandle prog, bgfx::ViewId viewId, uint64_t blendState)
{
    if (!cmd.text || !font)
        return;

    const float scale = cmd.fontSize / font->nominalSize();
    const uint32_t rgba = packColor(cmd.color);

    float cursorX = cmd.position.x;
    const float lineTopY = cmd.position.y;
    uint32_t prev = 0;

    const char* p = cmd.text;
    while (*p)
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

        const GlyphMetrics* g = font->getGlyph(cp);
        if (!g)
            continue;
        if (prev)
            cursorX += font->getKerning(prev, cp) * scale;

        const auto* sd = font->getGlyphSlugData(cp);
        if (!sd || sd->curveCount == 0 || g->size.x <= 0.f || g->size.y <= 0.f)
        {
            cursorX += g->advance * scale;
            prev = cp;
            continue;
        }

        // Screen-space corners in y-down line-relative space.
        const float x0 = cursorX + g->offset.x * scale;
        const float y0 = lineTopY + g->offset.y * scale;
        const float x1 = x0 + g->size.x * scale;
        const float y1 = y0 + g->size.y * scale;

        // Per-vertex TEXCOORD0 = font-space corners. The slug shader reads
        // these as glyph-local coordinates that match the curve buffer.
        // Top-left vertex (screen y0) → top of glyph in font space (fontTop).
        // Bottom-left vertex (screen y1) → bottom of glyph (fontTop - fontH).
        const float fL = sd->fontLeft;
        const float fR = sd->fontLeft + sd->fontWidth;
        const float fT = sd->fontTop;
        const float fB = sd->fontTop - sd->fontHeight;

        if (!bgfx::getAvailTransientVertexBuffer(4, layout_) ||
            !bgfx::getAvailTransientIndexBuffer(6))
        {
            cursorX += g->advance * scale;
            prev = cp;
            continue;
        }

        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};
        bgfx::allocTransientVertexBuffer(&tvb, 4, layout_);
        bgfx::allocTransientIndexBuffer(&tib, 6);

        auto* verts = reinterpret_cast<UiVertex*>(tvb.data);
        verts[0] = {x0, y0, fL, fT, rgba};
        verts[1] = {x1, y0, fR, fT, rgba};
        verts[2] = {x1, y1, fR, fB, rgba};
        verts[3] = {x0, y1, fL, fB, rgba};

        auto* idx = reinterpret_cast<uint16_t*>(tib.data);
        idx[0] = 0;
        idx[1] = 1;
        idx[2] = 2;
        idx[3] = 0;
        idx[4] = 2;
        idx[5] = 3;

        // Per-glyph uniform set + bind curve buffer + submit. Slug doesn't
        // use sampler 0 (s_texture); we still bind whiteTex_ to silence
        // any "no texture bound" warnings on backends that complain.
        bgfx::setTexture(0, s_texture_, whiteTex_);
        font->bindResources();
        font->setCurrentGlyph(sd->curveOffset, sd->curveCount);
        bgfx::setState(blendState);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::submit(viewId, prog);

        cursorX += g->advance * scale;
        prev = cp;
    }
}

}  // namespace engine::ui
