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
    uint8_t transparent = 0;     // 1 = alpha-blended (transparent pass, back-to-front sorted)
    uint8_t _pad[3];             // explicit padding — alignment to next uint32_t boundary
    uint32_t albedoMapId = 0;    // RenderResources texture ID (0 = none)
    uint32_t normalMapId = 0;
    uint32_t ormMapId = 0;       // G=roughness, B=metallic (R ignored per glTF spec)
    uint32_t emissiveMapId = 0;  // emissive color texture
    uint32_t occlusionMapId = 0; // separate occlusion texture (R=AO)
};
// Vec4(16) + float(4)*3 + uint8(1) + pad(3) + uint32(4)*5 = 52
static_assert(sizeof(Material) == 52, "Material layout changed — update padding");

}  // namespace engine::rendering
