#pragma once

#include <bgfx/bgfx.h>

#include "engine/rendering/GpuFeatures.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Vertex layout declarations for the two-stream vertex format.
//
// Stream 0 — position only (12 bytes/vertex).
//   Depth-only passes (shadow maps, depth prepass) bind only this stream,
//   avoiding any surface attribute fetch for those passes.
//
// Stream 1 — surface attributes (12 bytes/vertex with half UVs, 16 with float).
//   snorm16×2  normal    — oct-encoded unit normal
//   snorm8×4   tangent   — oct-encoded tangent XY + sign byte
//   float16×2  UV        — or float32×2 on GPUs without half-precision attribs
//
// Total: 24 bytes/vertex (half UV) vs 48 bytes/vertex naïve — 50% bandwidth reduction.
// ---------------------------------------------------------------------------

[[nodiscard]] inline bgfx::VertexLayout positionLayout()
{
    bgfx::VertexLayout l;
    l.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    return l;  // stride: 12 bytes
}

// surfaceLayout takes GpuFeatures to select float16 vs float32 UV encoding.
// All other attributes (snorm16 normal, snorm8 tangent) are universally supported
// by Metal and Vulkan — no per-platform branch needed for them.
[[nodiscard]] inline bgfx::VertexLayout surfaceLayout(const GpuFeatures& gpu)
{
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Normal, 2, bgfx::AttribType::Int16, /*normalized=*/true)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .add(bgfx::Attrib::TexCoord0, 2,
             gpu.halfPrecisionAttribs ? bgfx::AttribType::Half : bgfx::AttribType::Float)
        .end();
    return l;  // stride: 12 bytes (half UV) or 16 bytes (float UV fallback)
}

// Stream 2 -- skinning data (optional, only for skinned meshes).
//   4x uint8 bone indices (supports up to 256 bones per skeleton)
//   4x uint8 bone weights (normalized: 0-255 mapped to 0.0-1.0)
// Total: 8 bytes per vertex.
[[nodiscard]] inline bgfx::VertexLayout skinningLayout()
{
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, /*normalized=*/false)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .end();
    return l;  // stride: 8 bytes
}

}  // namespace engine::rendering
