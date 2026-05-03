#include "engine/ui/BitmapFont.h"

#include <bgfx/bgfx.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/rendering/ShaderLoader.h"

// stb_image — compile privately with STB_IMAGE_STATIC so the functions
// become TU-local and don't collide with engine_assets/TextureLoader.cpp
// (which carries the non-static copy) or with MsdfFont.cpp inside the
// same engine_ui library.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

namespace engine::ui
{

// ===========================================================================
// Embedded 8x8 ASCII font — font8x8_basic by Daniel Hepper (public domain).
// Covers codepoints 0x20..0x7F. One byte per row, LSB = leftmost pixel.
// Source: https://github.com/dhepper/font8x8  (public domain)
// Only the printable range 0x20..0x7E is used here; 0x7F is a rubout box.
// ===========================================================================

namespace
{

// clang-format off
constexpr std::uint8_t kFont8x8[96][8] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, // U+0020 (space)
    { 0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00 }, // U+0021 (!)
    { 0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00 }, // U+0022 (")
    { 0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00 }, // U+0023 (#)
    { 0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00 }, // U+0024 ($)
    { 0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00 }, // U+0025 (%)
    { 0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00 }, // U+0026 (&)
    { 0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00 }, // U+0027 (')
    { 0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00 }, // U+0028 (()
    { 0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00 }, // U+0029 ())
    { 0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00 }, // U+002A (*)
    { 0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00 }, // U+002B (+)
    { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06 }, // U+002C (,)
    { 0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00 }, // U+002D (-)
    { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00 }, // U+002E (.)
    { 0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00 }, // U+002F (/)
    { 0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00 }, // U+0030 (0)
    { 0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00 }, // U+0031 (1)
    { 0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00 }, // U+0032 (2)
    { 0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00 }, // U+0033 (3)
    { 0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00 }, // U+0034 (4)
    { 0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00 }, // U+0035 (5)
    { 0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00 }, // U+0036 (6)
    { 0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00 }, // U+0037 (7)
    { 0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00 }, // U+0038 (8)
    { 0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00 }, // U+0039 (9)
    { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00 }, // U+003A (:)
    { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06 }, // U+003B (;)
    { 0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00 }, // U+003C (<)
    { 0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00 }, // U+003D (=)
    { 0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00 }, // U+003E (>)
    { 0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00 }, // U+003F (?)
    { 0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00 }, // U+0040 (@)
    { 0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00 }, // U+0041 (A)
    { 0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00 }, // U+0042 (B)
    { 0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00 }, // U+0043 (C)
    { 0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00 }, // U+0044 (D)
    { 0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00 }, // U+0045 (E)
    { 0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00 }, // U+0046 (F)
    { 0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00 }, // U+0047 (G)
    { 0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00 }, // U+0048 (H)
    { 0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 }, // U+0049 (I)
    { 0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00 }, // U+004A (J)
    { 0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00 }, // U+004B (K)
    { 0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00 }, // U+004C (L)
    { 0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00 }, // U+004D (M)
    { 0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00 }, // U+004E (N)
    { 0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00 }, // U+004F (O)
    { 0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00 }, // U+0050 (P)
    { 0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00 }, // U+0051 (Q)
    { 0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00 }, // U+0052 (R)
    { 0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00 }, // U+0053 (S)
    { 0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 }, // U+0054 (T)
    { 0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00 }, // U+0055 (U)
    { 0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00 }, // U+0056 (V)
    { 0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00 }, // U+0057 (W)
    { 0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00 }, // U+0058 (X)
    { 0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00 }, // U+0059 (Y)
    { 0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00 }, // U+005A (Z)
    { 0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00 }, // U+005B ([)
    { 0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00 }, // U+005C (\)
    { 0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00 }, // U+005D (])
    { 0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00 }, // U+005E (^)
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF }, // U+005F (_)
    { 0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00 }, // U+0060 (`)
    { 0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00 }, // U+0061 (a)
    { 0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00 }, // U+0062 (b)
    { 0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00 }, // U+0063 (c)
    { 0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00 }, // U+0064 (d)
    { 0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00 }, // U+0065 (e)
    { 0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00 }, // U+0066 (f)
    { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F }, // U+0067 (g)
    { 0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00 }, // U+0068 (h)
    { 0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00 }, // U+0069 (i)
    { 0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E }, // U+006A (j)
    { 0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00 }, // U+006B (k)
    { 0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 }, // U+006C (l)
    { 0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00 }, // U+006D (m)
    { 0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00 }, // U+006E (n)
    { 0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00 }, // U+006F (o)
    { 0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F }, // U+0070 (p)
    { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78 }, // U+0071 (q)
    { 0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00 }, // U+0072 (r)
    { 0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00 }, // U+0073 (s)
    { 0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00 }, // U+0074 (t)
    { 0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00 }, // U+0075 (u)
    { 0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00 }, // U+0076 (v)
    { 0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00 }, // U+0077 (w)
    { 0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00 }, // U+0078 (x)
    { 0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F }, // U+0079 (y)
    { 0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00 }, // U+007A (z)
    { 0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00 }, // U+007B ({)
    { 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00 }, // U+007C (|)
    { 0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00 }, // U+007D (})
    { 0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00 }, // U+007E (~)
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, // U+007F (del)
};
// clang-format on

// Pack a single 8x8 glyph row-major at (cellX, cellY) into an RGBA8 atlas.
// Source uses LSB-first bit ordering. "On" pixels become fully-opaque
// white; "off" pixels become fully-transparent white. This way the sprite
// shader's `texColor * v_color0` produces the glyph in the widget's color
// with correct alpha blending.
void blitGlyph8x8(std::uint8_t* atlas, int atlasW, int cellX, int cellY,
                  const std::uint8_t glyph[8])
{
    for (int row = 0; row < 8; ++row)
    {
        const std::uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col)
        {
            const bool on = (bits >> col) & 1u;
            const int px = ((cellY + row) * atlasW + (cellX + col)) * 4;
            atlas[px + 0] = 0xFF;
            atlas[px + 1] = 0xFF;
            atlas[px + 2] = 0xFF;
            atlas[px + 3] = on ? 0xFF : 0x00;
        }
    }
}

// --- Minimal BMFont ".fnt" text-format parser ------------------------------

// Extracts an integer value for `key=` from a BMFont line. Returns fallback
// on miss.
int parseIntKey(const std::string& line, const char* key, int fallback = 0)
{
    const auto pos = line.find(key);
    if (pos == std::string::npos)
        return fallback;
    const auto eq = line.find('=', pos);
    if (eq == std::string::npos)
        return fallback;
    return std::atoi(line.c_str() + eq + 1);
}

float parseFloatKey(const std::string& line, const char* key, float fallback = 0.f)
{
    const auto pos = line.find(key);
    if (pos == std::string::npos)
        return fallback;
    const auto eq = line.find('=', pos);
    if (eq == std::string::npos)
        return fallback;
    return static_cast<float>(std::atof(line.c_str() + eq + 1));
}

}  // namespace

// ===========================================================================
// BitmapFont
// ===========================================================================

BitmapFont::~BitmapFont()
{
    shutdown();
}

void BitmapFont::shutdown()
{
    if (bgfx::isValid(atlas_))
    {
        bgfx::destroy(atlas_);
        atlas_ = BGFX_INVALID_HANDLE;
    }
    if (ownsProgram_ && bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
    }
    program_ = BGFX_INVALID_HANDLE;
    ownsProgram_ = false;
    glyphs_.clear();
    kerning_.clear();
    lineHeight_ = 0.f;
    nominalSize_ = 0.f;
}

const GlyphMetrics* BitmapFont::getGlyph(uint32_t codepoint) const
{
    const auto it = glyphs_.find(codepoint);
    if (it == glyphs_.end())
        return nullptr;
    return &it->second;
}

float BitmapFont::getKerning(uint32_t left, uint32_t right) const
{
    const uint64_t key = (static_cast<uint64_t>(left) << 32u) | right;
    const auto it = kerning_.find(key);
    return it == kerning_.end() ? 0.f : it->second;
}

// ---------------------------------------------------------------------------
// loadFromFile — BMFont ".fnt" (text) + atlas PNG
// ---------------------------------------------------------------------------

bool BitmapFont::loadFromFile(const char* fntPath, const char* atlasPath)
{
    shutdown();

    std::FILE* fp = std::fopen(fntPath, "rb");
    if (!fp)
        return false;

    int atlasW = 0;
    int atlasH = 0;

    char lineBuf[1024];
    while (std::fgets(lineBuf, sizeof(lineBuf), fp))
    {
        std::string line(lineBuf);

        if (line.rfind("info", 0) == 0)
        {
            nominalSize_ = parseFloatKey(line, "size=", 16.f);
            if (nominalSize_ < 0.f)
                nominalSize_ = -nominalSize_;
        }
        else if (line.rfind("common", 0) == 0)
        {
            lineHeight_ = parseFloatKey(line, "lineHeight=", 16.f);
            atlasW = parseIntKey(line, "scaleW=", 0);
            atlasH = parseIntKey(line, "scaleH=", 0);
        }
        else if (line.rfind("char ", 0) == 0)
        {
            GlyphMetrics g{};
            const uint32_t id = static_cast<uint32_t>(parseIntKey(line, "id="));
            const int x = parseIntKey(line, "x=");
            const int y = parseIntKey(line, "y=");
            const int w = parseIntKey(line, "width=");
            const int h = parseIntKey(line, "height=");
            const int xoff = parseIntKey(line, "xoffset=");
            const int yoff = parseIntKey(line, "yoffset=");
            const int xadv = parseIntKey(line, "xadvance=");
            if (atlasW > 0 && atlasH > 0)
            {
                g.uvRect = {static_cast<float>(x) / atlasW, static_cast<float>(y) / atlasH,
                            static_cast<float>(x + w) / atlasW, static_cast<float>(y + h) / atlasH};
            }
            g.size = {static_cast<float>(w), static_cast<float>(h)};
            g.offset = {static_cast<float>(xoff), static_cast<float>(yoff)};
            g.advance = static_cast<float>(xadv);
            glyphs_[id] = g;
        }
        else if (line.rfind("kerning ", 0) == 0)
        {
            const uint32_t first = static_cast<uint32_t>(parseIntKey(line, "first="));
            const uint32_t second = static_cast<uint32_t>(parseIntKey(line, "second="));
            const float amount = parseFloatKey(line, "amount=");
            const uint64_t key = (static_cast<uint64_t>(first) << 32u) | second;
            kerning_[key] = amount;
        }
    }
    std::fclose(fp);

    // Load atlas PNG as RGBA8 (BMFont atlases vary in channel layout; RGBA8
    // is the simplest route for bgfx and keeps the sprite shader happy).
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(atlasPath, &w, &h, &channels, 4);
    if (!pixels)
        return false;

    const bgfx::Memory* mem = bgfx::copy(pixels, static_cast<uint32_t>(w * h * 4));
    stbi_image_free(pixels);

    atlas_ = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h), false, 1,
                                   bgfx::TextureFormat::RGBA8,
                                   BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT, mem);

    // ShaderLoader returns engine::rendering::ProgramHandle (bgfx-free
    // wrapper); widen to bgfx for the engine-internal storage member.
    program_ = bgfx::ProgramHandle{engine::rendering::loadSpriteProgram().idx};
    ownsProgram_ = true;
    if (lineHeight_ <= 0.f)
        lineHeight_ = nominalSize_;
    return bgfx::isValid(atlas_) && bgfx::isValid(program_);
}

// ---------------------------------------------------------------------------
// createDebugFont — procedural 8x8 ASCII atlas
// ---------------------------------------------------------------------------
//
// Atlas layout: 16 glyphs per row, 6 rows = 96 glyphs covering 0x20..0x7F.
// Each cell is 8x8 pixels. Atlas is 128x48 R8.

bool BitmapFont::createDebugFont()
{
    shutdown();

    constexpr int kCellW = 8;
    constexpr int kCellH = 8;
    constexpr int kCols = 16;
    constexpr int kRows = 6;
    constexpr int kAtlasW = kCellW * kCols;  // 128
    constexpr int kAtlasH = kCellH * kRows;  // 48

    std::vector<std::uint8_t> pixels(kAtlasW * kAtlasH * 4, 0);

    // Nominal size: 16 px tall (we upscale the 8-px source 2x logically so
    // text looks reasonable at the default 16px font size).
    nominalSize_ = 16.f;
    lineHeight_ = 18.f;

    for (int i = 0; i < 96; ++i)
    {
        const uint32_t codepoint = 0x20u + static_cast<uint32_t>(i);
        const int cellX = (i % kCols) * kCellW;
        const int cellY = (i / kCols) * kCellH;
        blitGlyph8x8(pixels.data(), kAtlasW, cellX, cellY, kFont8x8[i]);

        GlyphMetrics g{};
        g.uvRect = {static_cast<float>(cellX) / kAtlasW, static_cast<float>(cellY) / kAtlasH,
                    static_cast<float>(cellX + kCellW) / kAtlasW,
                    static_cast<float>(cellY + kCellH) / kAtlasH};
        // Glyph quad at nominal size: 16 px tall, 16 px wide (2x the 8x8
        // source). Space has zero visible size but real advance.
        g.size = (codepoint == 0x20u) ? math::Vec2{0.f, 0.f} : math::Vec2{16.f, 16.f};
        g.offset = {0.f, 0.f};
        g.advance = 16.f;
        glyphs_[codepoint] = g;
    }

    // Bgfx wants an immutable memory ref for createTexture2D; copy the atlas.
    const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size()));

    atlas_ = bgfx::createTexture2D(kAtlasW, kAtlasH, false, 1, bgfx::TextureFormat::RGBA8,
                                   BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT, mem);

    // Under the Noop renderer (headless tests) createTexture2D returns an
    // invalid handle — that's fine, the renderer will early-out before
    // binding resources. Glyph metrics still resolve correctly so tests
    // and measurement work.
    // ShaderLoader returns engine::rendering::ProgramHandle (bgfx-free
    // wrapper); widen to bgfx for the engine-internal storage member.
    program_ = bgfx::ProgramHandle{engine::rendering::loadSpriteProgram().idx};
    ownsProgram_ = true;

    return true;
}

}  // namespace engine::ui
