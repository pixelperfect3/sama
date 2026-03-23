#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

#include "engine/ecs/Registry.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/SpriteBatcher.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// UiRenderSystem
//
// Reads:  SpriteComponent, WorldTransformComponent
// Writes: nothing (submits draw calls to kViewUi via SpriteBatcher)
//
// Per frame:
//   1. Configure view 14 (kViewUi): no clear, orthographic projection with
//      top-left origin (0,0) to (screenWidth, screenHeight).
//   2. Iterate all entities with both SpriteComponent and WorldTransformComponent
//      and add them to the SpriteBatcher.
//   3. Flush the batcher: sorts by textureId/sortZ, emits transient geometry.
// ---------------------------------------------------------------------------

class UiRenderSystem
{
public:
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle spriteProgram,
                bgfx::UniformHandle s_texture, uint16_t screenWidth, uint16_t screenHeight);

private:
    SpriteBatcher batcher_;
};

}  // namespace engine::rendering
