#include "engine/ui/MsdfFont.h"

#include <bgfx/bgfx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/io/Json.h"
#include "engine/rendering/ShaderLoader.h"

// stb_image — compile privately with STB_IMAGE_STATIC so the symbols get
// internal linkage and do not collide with engine_assets/TextureLoader.cpp,
// which ships its own non-static copy.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

namespace engine::ui
{

namespace
{

// Pack two 32-bit codepoints into a 64-bit kerning-pair key.
constexpr uint64_t makeKerningKey(uint32_t left, uint32_t right) noexcept
{
    return (static_cast<uint64_t>(left) << 32) | static_cast<uint64_t>(right);
}

// Read an entire file into a byte buffer. Returns an empty vector on failure.
// Used for the atlas PNG; the JSON metrics file goes through JsonDocument's
// own parseFile() helper.
std::vector<uint8_t> readFileBytes(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f)
        return {};

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> bytes;
    if (size > 0)
    {
        bytes.resize(static_cast<size_t>(size));
        const size_t got = std::fread(bytes.data(), 1, static_cast<size_t>(size), f);
        if (got != static_cast<size_t>(size))
            bytes.clear();
    }
    std::fclose(f);
    return bytes;
}

}  // namespace

// ---------------------------------------------------------------------------
// loadFromFile
// ---------------------------------------------------------------------------

bool MsdfFont::loadFromFile(const char* metricsPath, const char* atlasPath)
{
    const std::vector<uint8_t> jsonBytes = readFileBytes(metricsPath);
    const std::vector<uint8_t> pngBytes = readFileBytes(atlasPath);
    if (jsonBytes.empty() || pngBytes.empty())
        return false;
    return loadFromMemory(jsonBytes.data(), jsonBytes.size(), pngBytes.data(), pngBytes.size());
}

bool MsdfFont::loadFromMemory(const void* jsonData, std::size_t jsonSize, const void* pngData,
                              std::size_t pngSize)
{
    shutdown();

    // -- 1) Parse JSON metrics --------------------------------------------
    io::JsonDocument doc;
    if (!doc.parse(static_cast<const char*>(jsonData), jsonSize))
        return false;

    const io::JsonValue root = doc.root();
    if (!root.isObject())
        return false;

    // atlas { type, distanceRange, size, width, height, yOrigin }
    const io::JsonValue atlas = root["atlas"];
    if (!atlas.isObject())
        return false;

    const float atlasWidth = atlas["width"].getFloat(0.f);
    const float atlasHeight = atlas["height"].getFloat(0.f);
    if (atlasWidth <= 0.f || atlasHeight <= 0.f)
        return false;
    atlasWidth_ = atlasWidth;
    atlasHeight_ = atlasHeight;

    nominalSize_ = atlas["size"].getFloat(0.f);
    distanceRange_ = atlas["distanceRange"].getFloat(4.f);

    // yOrigin: "bottom" means atlas Y values run upward from the bottom edge
    // of the image; "top" means they run downward from the top. bgfx samples
    // with (0,0) at top-left, so we must flip on "bottom".
    bool flipY = true;
    if (atlas.hasMember("yOrigin") && atlas["yOrigin"].isString())
    {
        const char* yo = atlas["yOrigin"].getString();
        flipY = (yo != nullptr && std::strcmp(yo, "bottom") == 0);
    }

    // metrics { emSize, lineHeight, ascender, descender, ... }
    // lineHeight is reported in em units; convert to pixels using nominalSize.
    // We also need ascender to convert per-glyph plane bounds from
    // baseline-relative y-up coordinates into the y-down "distance from top
    // of line" convention that BitmapFont uses (and that UiRenderer expects
    // when it positions glyph quads relative to cmd.position).
    float ascenderEm = 0.95f;  // sane default if metrics block is missing
    const io::JsonValue metrics = root["metrics"];
    if (metrics.isObject())
    {
        const float emLineHeight = metrics["lineHeight"].getFloat(1.2f);
        lineHeight_ = emLineHeight * nominalSize_;
        ascenderEm = metrics["ascender"].getFloat(0.95f);
    }
    else
    {
        lineHeight_ = 1.2f * nominalSize_;
    }
    const float ascenderPx = ascenderEm * nominalSize_;

    // glyphs[] — each entry has advance + planeBounds (em units) + atlasBounds
    // (atlas pixels). Missing planeBounds means a whitespace glyph with no
    // visible quad; we still record the advance.
    const io::JsonValue glyphs = root["glyphs"];
    if (!glyphs.isArray())
        return false;

    for (auto gv : glyphs)
    {
        if (!gv.isObject())
            continue;

        const uint32_t codepoint = gv["unicode"].getUint(0);
        GlyphMetrics m{};
        m.advance = gv["advance"].getFloat(0.f) * nominalSize_;

        if (gv.hasMember("planeBounds") && gv["planeBounds"].isObject())
        {
            const io::JsonValue pb = gv["planeBounds"];
            const float pl = pb["left"].getFloat(0.f);
            const float pr = pb["right"].getFloat(0.f);
            const float pt = pb["top"].getFloat(0.f);
            const float pb_ = pb["bottom"].getFloat(0.f);

            // Plane bounds are em units with Y-up (baseline origin, positive =
            // above baseline). UiRenderer interprets cmd.position as the TOP
            // OF THE LINE in y-down screen space, and uses GlyphMetrics::offset
            // as a y-down delta from that point to the top-left corner of the
            // glyph quad. Convert: the top of the glyph in y-down line-space
            // is (ascender - planeBounds.top), the left is planeBounds.left.
            const float widthPx = (pr - pl) * nominalSize_;
            const float heightPx = (pt - pb_) * nominalSize_;
            m.size = math::Vec2{widthPx, heightPx};
            m.offset = math::Vec2{pl * nominalSize_, ascenderPx - pt * nominalSize_};
        }

        if (gv.hasMember("atlasBounds") && gv["atlasBounds"].isObject())
        {
            const io::JsonValue ab = gv["atlasBounds"];
            const float al = ab["left"].getFloat(0.f);
            const float ar = ab["right"].getFloat(0.f);
            const float at = ab["top"].getFloat(0.f);
            const float ab_ = ab["bottom"].getFloat(0.f);

            const float u0 = al / atlasWidth;
            const float u1 = ar / atlasWidth;
            float v0, v1;
            if (flipY)
            {
                // Atlas Y runs from the bottom: top > bottom numerically, and
                // we want the glyph's visual top to map to the smaller v.
                v0 = 1.f - (at / atlasHeight);
                v1 = 1.f - (ab_ / atlasHeight);
            }
            else
            {
                v0 = at / atlasHeight;
                v1 = ab_ / atlasHeight;
            }
            m.uvRect = math::Vec4{u0, v0, u1, v1};
        }

        glyphs_[codepoint] = m;
    }

    // kerning[] — optional, each entry { unicode1, unicode2, advance (em) }.
    if (root.hasMember("kerning") && root["kerning"].isArray())
    {
        for (auto kv : root["kerning"])
        {
            if (!kv.isObject())
                continue;
            const uint32_t l = kv["unicode1"].getUint(0);
            const uint32_t r = kv["unicode2"].getUint(0);
            const float adv = kv["advance"].getFloat(0.f) * nominalSize_;
            kerning_[makeKerningKey(l, r)] = adv;
        }
    }

    // -- 2) Decode the atlas PNG ------------------------------------------
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels =
        stbi_load_from_memory(static_cast<const stbi_uc*>(pngData), static_cast<int>(pngSize), &w,
                              &h, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        shutdown();
        return false;
    }

    // -- 3) Upload the atlas texture --------------------------------------
    // Bilinear filtering is critical for MSDF quality; the fragment shader
    // relies on smoothly interpolated distances between texels.
    if (bgfx::getRendererType() != bgfx::RendererType::Noop)
    {
        const bgfx::Memory* mem = bgfx::copy(pixels, static_cast<uint32_t>(w * h * 4));
        atlas_ = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h), false, 1,
                                       bgfx::TextureFormat::RGBA8,
                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
    }
    stbi_image_free(pixels);

    // -- 4) Create shader + uniforms --------------------------------------
    if (bgfx::getRendererType() != bgfx::RendererType::Noop)
    {
        // ShaderLoader returns the bgfx-free engine::rendering::ProgramHandle
        // wrapper; widen back to bgfx for the engine-internal storage member.
        program_ = bgfx::ProgramHandle{engine::rendering::loadMsdfProgram().idx};
        s_texture_ = bgfx::createUniform("s_texture", bgfx::UniformType::Sampler);
        u_msdfParams_ = bgfx::createUniform("u_msdfParams", bgfx::UniformType::Vec4);
    }

    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void MsdfFont::shutdown()
{
    glyphs_.clear();
    kerning_.clear();
    lineHeight_ = 0.f;
    nominalSize_ = 0.f;
    distanceRange_ = 4.f;
    atlasWidth_ = 0.f;
    atlasHeight_ = 0.f;

    if (bgfx::isValid(atlas_))
    {
        bgfx::destroy(atlas_);
        atlas_ = BGFX_INVALID_HANDLE;
    }
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
    if (bgfx::isValid(u_msdfParams_))
    {
        bgfx::destroy(u_msdfParams_);
        u_msdfParams_ = BGFX_INVALID_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// IFont accessors
// ---------------------------------------------------------------------------

const GlyphMetrics* MsdfFont::getGlyph(uint32_t codepoint) const
{
    const auto it = glyphs_.find(codepoint);
    if (it == glyphs_.end())
        return nullptr;
    return &it->second;
}

float MsdfFont::getKerning(uint32_t left, uint32_t right) const
{
    const auto it = kerning_.find(makeKerningKey(left, right));
    if (it == kerning_.end())
        return 0.f;
    return it->second;
}

void MsdfFont::bindResources() const
{
    if (!bgfx::isValid(u_msdfParams_))
        return;

    // u_msdfParams: .x = atlas pixel range (distanceRange)
    //               .yz = atlas texture dimensions in pixels
    // The fragment shader uses yz together with fwidth(v_texcoord0) to compute
    // a per-fragment screen-pixel range, so edges stay crisp at any draw size.
    const float params[4] = {distanceRange_, atlasWidth_, atlasHeight_, 0.f};
    bgfx::setUniform(u_msdfParams_, params);
}

}  // namespace engine::ui
