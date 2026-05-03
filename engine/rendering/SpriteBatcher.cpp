#include "engine/rendering/SpriteBatcher.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Sprite vertex — matches the layout returned by spriteLayout().
// ---------------------------------------------------------------------------

struct SpriteVertex
{
    float x, y;     // position (screen / world XY)
    float u, v;     // UV coordinates
    uint32_t rgba;  // packed ABGR (bgfx uint8 color convention)
};

// ---------------------------------------------------------------------------
// spriteLayout
// ---------------------------------------------------------------------------

bgfx::VertexLayout spriteLayout()
{
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)      // 8 bytes
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)     // 8 bytes
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)  // 4 bytes (normalized)
        .end();
    return l;
}

// ---------------------------------------------------------------------------
// Helper: pack Vec4 [0,1] RGBA into uint32_t ABGR (bgfx Color0 convention).
// ---------------------------------------------------------------------------

static uint32_t packRgba(const math::Vec4& c)
{
    // GLM_FORCE_XYZW_ONLY disables .r/.g/.b/.a — use .x/.y/.z/.w instead.
    const auto r = static_cast<uint32_t>(glm::clamp(c.x, 0.f, 1.f) * 255.f + 0.5f);
    const auto g = static_cast<uint32_t>(glm::clamp(c.y, 0.f, 1.f) * 255.f + 0.5f);
    const auto b = static_cast<uint32_t>(glm::clamp(c.z, 0.f, 1.f) * 255.f + 0.5f);
    const auto a = static_cast<uint32_t>(glm::clamp(c.w, 0.f, 1.f) * 255.f + 0.5f);
    // bgfx Attrib::Color0 with Uint8 normalized expects ABGR byte order.
    return (a << 24u) | (b << 16u) | (g << 8u) | r;
}

// ---------------------------------------------------------------------------
// SpriteBatcher
// ---------------------------------------------------------------------------

void SpriteBatcher::begin()
{
    sprites_.clear();
}

void SpriteBatcher::addSprite(const math::Mat4& transform, const SpriteComponent& sprite)
{
    sprites_.push_back(SpriteEntry{transform, sprite});
}

void SpriteBatcher::flush(bgfx::Encoder* enc, bgfx::ProgramHandle program,
                          bgfx::UniformHandle s_texture, const RenderResources& res)
{
    if (sprites_.empty())
        return;

    // Headless (Noop) renderer — transient buffers are unavailable.
    if (bgfx::getRendererType() == bgfx::RendererType::Noop)
        return;

    // 1. Sort: primary key = textureId, secondary key = sortZ ascending.
    std::sort(sprites_.begin(), sprites_.end(),
              [](const SpriteEntry& a, const SpriteEntry& b)
              {
                  if (a.sprite.textureId != b.sprite.textureId)
                      return a.sprite.textureId < b.sprite.textureId;
                  return a.sprite.sortZ < b.sprite.sortZ;
              });

    const bgfx::VertexLayout layout = spriteLayout();

    // 2. Walk the sorted list and emit one batch per contiguous textureId run.
    uint32_t batchStart = 0;
    const uint32_t total = static_cast<uint32_t>(sprites_.size());

    while (batchStart < total)
    {
        const uint32_t batchTexId = sprites_[batchStart].sprite.textureId;

        // Find end of contiguous run with the same textureId.
        uint32_t batchEnd = batchStart + 1;
        while (batchEnd < total && sprites_[batchEnd].sprite.textureId == batchTexId)
            ++batchEnd;

        const uint32_t count = batchEnd - batchStart;

        // 3. Allocate transient buffers.
        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};

        bgfx::allocTransientVertexBuffer(&tvb, count * 4, layout);
        bgfx::allocTransientIndexBuffer(&tib, count * 6);

        auto* verts = reinterpret_cast<SpriteVertex*>(tvb.data);
        auto* idx = reinterpret_cast<uint16_t*>(tib.data);

        // 4. Fill vertex and index data.
        for (uint32_t i = 0; i < count; ++i)
        {
            const SpriteEntry& entry = sprites_[batchStart + i];
            const SpriteComponent& spr = entry.sprite;
            const math::Mat4& m = entry.transform;

            const float u0 = spr.uvRect.x;
            const float v0 = spr.uvRect.y;
            const float u1 = spr.uvRect.z;
            const float v1 = spr.uvRect.w;
            const uint32_t rgba = packRgba(spr.color);

            // Unit quad corners in local space: (-0.5, -0.5) to (0.5, 0.5).
            const glm::vec4 corners[4] = {
                {-0.5f, -0.5f, 0.0f, 1.0f},  // bottom-left
                {0.5f, -0.5f, 0.0f, 1.0f},   // bottom-right
                {0.5f, 0.5f, 0.0f, 1.0f},    // top-right
                {-0.5f, 0.5f, 0.0f, 1.0f},   // top-left
            };

            const float uvs[4][2] = {
                {u0, v1},
                {u1, v1},
                {u1, v0},
                {u0, v0},
            };

            const uint32_t base = i * 4;
            for (uint32_t c = 0; c < 4; ++c)
            {
                const glm::vec4 wp = m * corners[c];
                verts[base + c] = SpriteVertex{wp.x, wp.y, uvs[c][0], uvs[c][1], rgba};
            }

            // Indices: two triangles — 0,1,2 and 0,2,3.
            const uint16_t v = static_cast<uint16_t>(base);
            idx[i * 6 + 0] = v + 0;
            idx[i * 6 + 1] = v + 1;
            idx[i * 6 + 2] = v + 2;
            idx[i * 6 + 3] = v + 0;
            idx[i * 6 + 4] = v + 2;
            idx[i * 6 + 5] = v + 3;
        }

        // 5. Bind texture.
        // textureId == 0 → white (tint-only) texture; other IDs are future
        // work once a full texture registry is added to RenderResources.
        if (bgfx::isValid(s_texture))
        {
            bgfx::TextureHandle invalid = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle tex =
                (batchTexId == 0) ? bgfx::TextureHandle{res.whiteTexture().idx} : invalid;
            enc->setTexture(0, s_texture, tex);
        }

        // 6. Set render state: alpha blend, no depth write.
        const uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

        enc->setState(state);
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->submit(kViewUi, program);

        batchStart = batchEnd;
    }

    sprites_.clear();
}

}  // namespace engine::rendering
