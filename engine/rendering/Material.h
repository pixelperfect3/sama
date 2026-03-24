#pragma once

#include <cstdint>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Material — PBR material parameters and texture handles.
//
// albedo.xyz = base color, albedo.w = opacity (1.0 = fully opaque).
// roughness and metallic are scalar multipliers combined with the ORM map.
// emissiveScale multiplies the albedo color to produce the emissive contribution.
//
// Texture IDs reference the RenderResources texture table.  0 = no texture;
// the shader uses a white 1×1 default so the scalar values are used as-is.
//
// ORM map packing: R = occlusion, G = roughness, B = metallic.
// ---------------------------------------------------------------------------

struct Material
{
    math::Vec4 albedo = math::Vec4(1.0f);  // .xyz = color, .w = opacity
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveScale = 0.0f;
    uint8_t _pad[4];           // explicit padding — alignment to next uint32_t boundary
    uint32_t albedoMapId = 0;  // RenderResources texture ID (0 = none)
    uint32_t normalMapId = 0;
    uint32_t ormMapId = 0;  // R=occlusion, G=roughness, B=metallic
};
// Vec4(16) + float(4) + float(4) + float(4) + pad(4) + uint32(4) + uint32(4) + uint32(4) = 44
static_assert(sizeof(Material) == 44, "Material layout changed — update padding");

}  // namespace engine::rendering
