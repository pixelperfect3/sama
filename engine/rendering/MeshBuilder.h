#pragma once

#include <cstdint>
#include <vector>

#include "engine/math/Types.h"
#include "engine/rendering/Mesh.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// MeshData — CPU-side mesh data ready for upload to the GPU.
//
// Stream 0 (positions) is mandatory.
// Stream 1 (surface attributes) is optional — if normals/tangents/uvs are all
// empty, no surfaceVbh is created and Mesh::surfaceVbh remains invalid.
//
// Encoding conventions (match the GPU vertex layout in VertexLayouts.h):
//   normals  — oct-encoded, 2× snorm16 per vertex (pairs in the vector)
//   tangents — oct-encoded, 3× snorm8 + sign byte, 4 values per vertex (quads)
//   uvs      — float16, 2× uint16_t per vertex (packed via glm::packHalf2x16)
//   indices  — 16-bit (uint16_t)
// ---------------------------------------------------------------------------

struct MeshData
{
    // Stream 0 — interleaved x,y,z floats; size must be divisible by 3.
    std::vector<float> positions;

    // Stream 1 — all three must be empty, or all three must be non-empty.
    // normals: 2 values per vertex (oct-encoded snorm16 pair)
    std::vector<int16_t> normals;
    // tangents: 4 values per vertex (oct-encoded unorm8 x3 + sign byte, bias+scale encoded)
    // bgfx::AttribType::Uint8 normalized maps [0,255]->[0,1]; shader remaps as 2*v-1.
    std::vector<uint8_t> tangents;
    // uvs: 2 uint16_t per vertex (float16 encoded)
    std::vector<uint16_t> uvs;

    // 16-bit index buffer
    std::vector<uint16_t> indices;

    // Axis-aligned bounding box in local space
    math::Vec3 boundsMin{};
    math::Vec3 boundsMax{};
};

// ---------------------------------------------------------------------------
// buildMesh — upload MeshData to the GPU via bgfx.
//
// Uses bgfx::copy() so the CPU buffers can be freed immediately after this
// call returns.  Requires bgfx to be initialized.
//
// Returns an invalid Mesh (isValid() == false) if positions or indices are
// empty, or if bgfx handle creation fails.
// ---------------------------------------------------------------------------
[[nodiscard]] Mesh buildMesh(const MeshData& data);

// ---------------------------------------------------------------------------
// makeCubeMeshData — unit cube (±0.5 on each axis) with 24 unique vertices
// (one per face corner, so normals are correct for lighting).
//
// All surface attributes are baked in:
//   normals  — face normals, oct-encoded to snorm16
//   tangents — per-face tangents, oct-encoded to snorm8 + sign
//   uvs      — [0,1] per face, encoded to float16
// ---------------------------------------------------------------------------
[[nodiscard]] MeshData makeCubeMeshData();

}  // namespace engine::rendering
