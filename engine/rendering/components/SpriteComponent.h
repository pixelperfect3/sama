#pragma once

#include <cstdint>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// SpriteComponent — renderable 2D sprite.
//
// Attach to any entity that also has a WorldTransformComponent.
// Position/rotation/scale come from the entity's WorldTransformComponent
// (.matrix).  For pure UI elements use screen-space coordinates and an
// orthographic camera.
//
// Layout (ordered largest-alignment-first, no implicit padding):
//   uvRect    — 16 bytes at offset  0
//   color     — 16 bytes at offset 16
//   textureId —  4 bytes at offset 32
//   sortZ     —  4 bytes at offset 36
//   _pad      —  4 bytes at offset 40
// Total: 44 bytes.
// ---------------------------------------------------------------------------

struct SpriteComponent  // offset  size
{
    math::Vec4 uvRect;   //  0      16   {u0, v0, u1, v1} in [0,1]
    math::Vec4 color;    // 16      16   RGBA tint [0,1]
    uint32_t textureId;  // 32       4   RenderResources texture ID (0 = white)
    float sortZ;         // 36       4   sort order within UI layer (lower = further back)
    uint8_t _pad[4];     // 40       4
};  // total: 44 bytes
static_assert(sizeof(SpriteComponent) == 44);

}  // namespace engine::rendering
