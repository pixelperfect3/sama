#pragma once

#include <bgfx/bgfx.h>

#include "engine/math/Types.h"
#include "engine/memory/InlinedVector.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/components/SpriteComponent.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// SpriteBatcher
//
// Collects sprites from ECS, sorts by textureId then sortZ, and builds
// batched transient vertex/index buffers for submission to kViewUi.
//
// Vertex layout: {float2 pos, float2 uv, uint8x4 color} = 20 bytes/vertex.
// Each sprite becomes 4 vertices + 6 indices (two triangles).
//
// Usage per frame:
//   batcher.begin();
//   for each sprite entity: batcher.addSprite(transform, sprite);
//   batcher.flush(enc, program, s_texture, res);
// ---------------------------------------------------------------------------

// Returns the shared vertex layout for sprite geometry.
[[nodiscard]] bgfx::VertexLayout spriteLayout();

class SpriteBatcher
{
public:
    // Reset the sprite list. Call once per frame before collecting sprites.
    void begin();

    // Enqueue a sprite for batched submission.
    void addSprite(const math::Mat4& transform, const SpriteComponent& sprite);

    // Sort, batch, and submit all enqueued sprites to kViewUi.
    // Clears the internal list after submission.
    //
    // In headless (Noop renderer) mode this is a no-op since transient buffers
    // cannot be allocated without a real backend.
    void flush(bgfx::Encoder* enc, bgfx::ProgramHandle program, bgfx::UniformHandle s_texture,
               const RenderResources& res);

private:
    struct SpriteEntry
    {
        math::Mat4 transform;
        SpriteComponent sprite;
    };

    memory::InlinedVector<SpriteEntry, 64> sprites_;
};

}  // namespace engine::rendering
