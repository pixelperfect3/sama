// ---------------------------------------------------------------------------
// SlugFont.cpp — minimum-viable Slug backend.
//
// See SlugFont.h for the big-picture rationale. This file is deliberately
// scoped to "extract curves + pack buffer + create program", nothing more.
// It does NOT touch UiRenderer, the bitmap/MSDF backends, or any widget
// code — those are owned by other agents. Follow-up work is tracked in
// docs/SLUG_NEXT_STEPS.md.
//
// Shader provenance: fs_slug.sc / vs_slug.sc are adapted from Eric
// Lengyel's MIT-licensed reference implementation at
// https://github.com/EricLengyel/Slug. Attribution lives in those files.
// ---------------------------------------------------------------------------

#include "engine/ui/SlugFont.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if SAMA_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#endif

#include "engine/rendering/ShaderLoader.h"

namespace engine::ui
{

namespace
{

#if SAMA_HAS_FREETYPE

// ------------------------------------------------------------------
// FT_Outline_Decompose callbacks
//
// FreeType walks the outline and calls our move/line/conic/cubic hooks
// for every segment. We record them into a flat vector of 3-point
// quadratic Bezier curves:
//   - move_to starts a new contour; we remember the contour-start point
//     and the current pen position.
//   - line_to emits a degenerate quadratic (p1 = midpoint(p0, p2)).
//   - conic_to emits a real quadratic.
//   - cubic_to (rare in TrueType) is approximated as a single quadratic
//     whose control point is the average of the two cubic handles. This
//     is lossy but fine for v1; most TrueType outlines never hit this.
// Coordinates come from FreeType in 26.6 fixed-point font units; we
// convert to float pixels by dividing by 64.
// ------------------------------------------------------------------

struct DecomposeState
{
    std::vector<float>* out = nullptr;  // appended as 3*Vec2 per curve
    float invUnit = 1.f / 64.f;
    FT_Vector pen{};
    uint32_t curveCount = 0;
};

inline float toPx(FT_Pos v, float invUnit)
{
    return static_cast<float>(v) * invUnit;
}

inline void pushCurve(DecomposeState* st, float p0x, float p0y, float p1x, float p1y, float p2x,
                      float p2y)
{
    st->out->push_back(p0x);
    st->out->push_back(p0y);
    st->out->push_back(p1x);
    st->out->push_back(p1y);
    st->out->push_back(p2x);
    st->out->push_back(p2y);
    // Pad to 8 floats per curve so the shader's texelFetch stride is a
    // clean 2 RGBA32F texels (see SlugFont.h kFloatsPerCurve).
    st->out->push_back(0.f);
    st->out->push_back(0.f);
    ++st->curveCount;
}

int ftMoveTo(const FT_Vector* to, void* user)
{
    auto* st = static_cast<DecomposeState*>(user);
    st->pen = *to;
    return 0;
}

int ftLineTo(const FT_Vector* to, void* user)
{
    auto* st = static_cast<DecomposeState*>(user);
    const float p0x = toPx(st->pen.x, st->invUnit);
    const float p0y = toPx(st->pen.y, st->invUnit);
    const float p2x = toPx(to->x, st->invUnit);
    const float p2y = toPx(to->y, st->invUnit);
    pushCurve(st, p0x, p0y, 0.5f * (p0x + p2x), 0.5f * (p0y + p2y), p2x, p2y);
    st->pen = *to;
    return 0;
}

int ftConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
{
    auto* st = static_cast<DecomposeState*>(user);
    const float p0x = toPx(st->pen.x, st->invUnit);
    const float p0y = toPx(st->pen.y, st->invUnit);
    const float p1x = toPx(control->x, st->invUnit);
    const float p1y = toPx(control->y, st->invUnit);
    const float p2x = toPx(to->x, st->invUnit);
    const float p2y = toPx(to->y, st->invUnit);
    pushCurve(st, p0x, p0y, p1x, p1y, p2x, p2y);
    st->pen = *to;
    return 0;
}

int ftCubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
{
    // Lossy cubic->quadratic: control := (c1 + c2) / 2. Acceptable for a v1
    // that targets TrueType fonts (which almost never emit cubics).
    auto* st = static_cast<DecomposeState*>(user);
    const float p0x = toPx(st->pen.x, st->invUnit);
    const float p0y = toPx(st->pen.y, st->invUnit);
    const float p1x = 0.5f * (toPx(c1->x, st->invUnit) + toPx(c2->x, st->invUnit));
    const float p1y = 0.5f * (toPx(c1->y, st->invUnit) + toPx(c2->y, st->invUnit));
    const float p2x = toPx(to->x, st->invUnit);
    const float p2y = toPx(to->y, st->invUnit);
    pushCurve(st, p0x, p0y, p1x, p1y, p2x, p2y);
    st->pen = *to;
    return 0;
}

#endif  // SAMA_HAS_FREETYPE

}  // anonymous namespace

SlugFont::~SlugFont()
{
    shutdown();
}

void SlugFont::shutdown()
{
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(curveBufferTexture_))
    {
        bgfx::destroy(curveBufferTexture_);
        curveBufferTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(s_curveBuffer_))
    {
        bgfx::destroy(s_curveBuffer_);
        s_curveBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(u_slugParams_))
    {
        bgfx::destroy(u_slugParams_);
        u_slugParams_ = BGFX_INVALID_HANDLE;
    }
    glyphs_.clear();
    slugData_.clear();
    curveBuffer_.clear();
    lineHeight_ = 0.f;
    nominalSize_ = 0.f;
    totalCurveCount_ = 0;
    loaded_ = false;
}

const GlyphMetrics* SlugFont::getGlyph(uint32_t codepoint) const
{
    const auto it = glyphs_.find(codepoint);
    return it == glyphs_.end() ? nullptr : &it->second;
}

float SlugFont::getKerning(uint32_t /*left*/, uint32_t /*right*/) const
{
    // Kerning not yet implemented for Slug — see SLUG_NEXT_STEPS.md.
    return 0.f;
}

const SlugFont::GlyphSlugData* SlugFont::getGlyphSlugData(uint32_t codepoint) const
{
    const auto it = slugData_.find(codepoint);
    return it == slugData_.end() ? nullptr : &it->second;
}

void SlugFont::bindResources() const
{
    // Sampler unit 1: curve buffer texture. Unit 0 is reserved by
    // UiRenderer for the atlas sampler used by bitmap/MSDF fonts — Slug
    // simply doesn't bind anything there.
    if (bgfx::isValid(s_curveBuffer_) && bgfx::isValid(curveBufferTexture_))
    {
        bgfx::setTexture(1, s_curveBuffer_, curveBufferTexture_);
    }
}

bool SlugFont::loadFromFile(const char* ttfPath, float pixelSize)
{
    shutdown();

#if !SAMA_HAS_FREETYPE
    (void)ttfPath;
    (void)pixelSize;
    return false;
#else
    if (ttfPath == nullptr || pixelSize <= 0.f)
        return false;

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0)
        return false;

    FT_Face face = nullptr;
    if (FT_New_Face(library, ttfPath, 0, &face) != 0)
    {
        FT_Done_FreeType(library);
        return false;
    }

    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return false;
    }

    nominalSize_ = pixelSize;
    lineHeight_ = static_cast<float>(face->size->metrics.height >> 6);

    // Pre-reserve: 256 floats per glyph is a reasonable starting guess for
    // a typical printable-ASCII font. The vector will grow as needed.
    curveBuffer_.reserve(256 * 95);

    FT_Outline_Funcs funcs{};
    funcs.move_to = &ftMoveTo;
    funcs.line_to = &ftLineTo;
    funcs.conic_to = &ftConicTo;
    funcs.cubic_to = &ftCubicTo;
    funcs.shift = 0;
    funcs.delta = 0;

    for (uint32_t cp = 32; cp <= 126; ++cp)
    {
        if (FT_Load_Char(face, cp, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0)
            continue;

        const FT_GlyphSlot slot = face->glyph;

        DecomposeState st;
        st.out = &curveBuffer_;
        st.invUnit = 1.f / 64.f;
        st.pen = FT_Vector{0, 0};
        st.curveCount = 0;
        const uint32_t startFloats = static_cast<uint32_t>(curveBuffer_.size());
        const uint32_t startCurve = startFloats / kFloatsPerCurve;

        FT_Outline_Decompose(&slot->outline, &funcs, &st);

        GlyphMetrics metrics{};
        metrics.uvRect = {0.f, 0.f, 0.f, 0.f};
        metrics.size = {static_cast<float>(slot->metrics.width >> 6),
                        static_cast<float>(slot->metrics.height >> 6)};
        metrics.offset = {static_cast<float>(slot->metrics.horiBearingX >> 6),
                          static_cast<float>(slot->metrics.horiBearingY >> 6)};
        metrics.advance = static_cast<float>(slot->metrics.horiAdvance >> 6);
        glyphs_.emplace(cp, metrics);

        GlyphSlugData sd{};
        sd.curveOffset = startCurve;
        sd.curveCount = st.curveCount;
        slugData_.emplace(cp, sd);
    }

    totalCurveCount_ = static_cast<uint32_t>(curveBuffer_.size() / kFloatsPerCurve);

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    // ------------------------------------------------------------------
    // Upload the curve data as an RGBA32F 2D texture. For v1 simplicity we
    // pick a square-ish layout: width = next power-of-two >= sqrt(texels),
    // height = ceil(texels / width). Each curve occupies 2 consecutive
    // RGBA32F texels (see kFloatsPerCurve).
    // ------------------------------------------------------------------
    const uint32_t totalTexels =
        std::max<uint32_t>(1u, static_cast<uint32_t>(curveBuffer_.size() / 4));

    uint32_t texW = 1;
    while (texW * texW < totalTexels)
        texW <<= 1;
    const uint32_t texH = (totalTexels + texW - 1) / texW;

    // Pad the float buffer up to texW * texH * 4 floats before upload so
    // bgfx reads a complete rectangle.
    const uint32_t requiredFloats = texW * texH * 4;
    if (curveBuffer_.size() < requiredFloats)
        curveBuffer_.resize(requiredFloats, 0.f);

    // In headless (Noop) mode, skip all bgfx resource creation. Loading
    // the font data is still useful — tests and tools can inspect curves
    // without a real GPU context.
    if (bgfx::getRendererType() != bgfx::RendererType::Noop)
    {
        const bgfx::Memory* mem = bgfx::copy(
            curveBuffer_.data(), static_cast<uint32_t>(curveBuffer_.size() * sizeof(float)));

        curveBufferTexture_ = bgfx::createTexture2D(
            static_cast<uint16_t>(texW), static_cast<uint16_t>(texH),
            /*hasMips=*/false,
            /*numLayers=*/1, bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP, mem);

        s_curveBuffer_ = bgfx::createUniform("s_slugCurves", bgfx::UniformType::Sampler);
        u_slugParams_ = bgfx::createUniform("u_slugParams", bgfx::UniformType::Vec4);

        program_ = engine::rendering::loadSlugProgram();
    }

    loaded_ = true;
    return true;
#endif  // SAMA_HAS_FREETYPE
}

}  // namespace engine::ui
