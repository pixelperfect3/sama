#pragma once

#include <bgfx/bgfx.h>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Mesh — live GPU resource holding two vertex buffer streams and an index buffer.
//
// positionVbh: Stream 0 — float3 position (12 bytes/vertex)
// surfaceVbh:  Stream 1 — oct-encoded normal/tangent + float16 UV (12 bytes/vertex)
//              May be BGFX_INVALID_HANDLE for depth-only geometry.
// ibh:         16-bit index buffer (32-bit if vertexCount > 65535).
//
// Meshes are owned by RenderResources, which calls destroy() on removal.
// Do not destroy handles manually outside of RenderResources.
// ---------------------------------------------------------------------------

struct Mesh
{
    bgfx::VertexBufferHandle positionVbh = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle surfaceVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;

    math::Vec3 boundsMin{};
    math::Vec3 boundsMax{};

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    // Returns true if the position stream and index buffer are valid.
    // surfaceVbh is optional (depth-only meshes may omit it).
    [[nodiscard]] bool isValid() const
    {
        return bgfx::isValid(positionVbh) && bgfx::isValid(ibh);
    }

    // Destroys all live bgfx handles and resets the struct to default state.
    // Safe to call multiple times — subsequent calls are no-ops.
    void destroy()
    {
        if (bgfx::isValid(positionVbh))
            bgfx::destroy(positionVbh);
        if (bgfx::isValid(surfaceVbh))
            bgfx::destroy(surfaceVbh);
        if (bgfx::isValid(ibh))
            bgfx::destroy(ibh);
        *this = Mesh{};
    }
};

}  // namespace engine::rendering
