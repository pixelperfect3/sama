#pragma once

#include "engine/rendering/HandleTypes.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// ShaderLoader — creates shader programs from embedded compiled bytecode.
//
// Shader bytecode is compiled offline by shaderc (via CMake custom commands)
// and embedded as C header arrays.  loadUnlitProgram() selects the correct
// bytecode for the current renderer backend at runtime.
//
// All returned handles are owned by the caller and must be destroyed with
// the engine renderer's destroy path when no longer needed.  The returned
// ProgramHandle is the bgfx-free public wrapper (see HandleTypes.h); the
// boundary conversion to bgfx::ProgramHandle happens inside ShaderLoader.cpp.
// ---------------------------------------------------------------------------

// Unlit program — position stream only, outputs a solid orange colour.
// Used for Phase 2 smoke tests.  Returns kInvalidProgram on failure.
[[nodiscard]] ProgramHandle loadUnlitProgram();

// PBR program — two-stream vertex (position + surface), GGX PBR fragment shader.
// Returns kInvalidProgram on failure or when running the Noop renderer.
[[nodiscard]] ProgramHandle loadPbrProgram();

// Sprite program — position + texcoord0 + color0 streams, samples s_texture
// and multiplies by vertex color.  Used by UiRenderSystem / SpriteBatcher.
// Returns kInvalidProgram on failure or in headless (Noop) mode.
[[nodiscard]] ProgramHandle loadSpriteProgram();

// MSDF text program — reuses vs_sprite for the vertex stream, samples an
// RGB multi-channel signed distance field atlas via s_texture, and runs
// the MSDF reconstruction fragment shader (median + smoothstep).  Used by
// MsdfFont.  Requires a Vec4 uniform u_msdfParams where x = distanceRange
// in atlas pixels, set per-batch via MsdfFont::bindResources().
// Returns kInvalidProgram on failure or in headless (Noop) mode.
[[nodiscard]] ProgramHandle loadMsdfProgram();

// Shadow program — depth-only, Stream 0 (position) only.
// Used by the shadow pass to render scene geometry into the shadow atlas.
// Returns kInvalidProgram on failure or in headless (Noop) mode.
[[nodiscard]] ProgramHandle loadShadowProgram();

// Skinned PBR program — same fragment shader as PBR, but vertex shader
// performs GPU skinning via u_model[] bone matrices + a_indices/a_weight.
// Returns kInvalidProgram on failure or in headless (Noop) mode.
[[nodiscard]] ProgramHandle loadSkinnedPbrProgram();

// Skinned shadow program — depth-only vertex shader with GPU skinning.
// Used by the shadow pass for skinned meshes (bone matrices via u_model[]).
// Returns kInvalidProgram on failure or in headless (Noop) mode.
[[nodiscard]] ProgramHandle loadSkinnedShadowProgram();

// Gizmo program — position + color0 passthrough.  Used for colored
// transform gizmo lines/arrows in the editor overlay.
// Returns kInvalidProgram on failure or in headless (Noop) mode.
[[nodiscard]] ProgramHandle loadGizmoProgram();

// Slug program — position + texcoord0 + color0 streams (same layout as
// sprite). Fragment shader evaluates packed quadratic Bezier curves from
// a buffer texture to produce vector-perfect glyph coverage at any scale.
// Used by SlugFont. Returns kInvalidProgram on failure or in headless
// (Noop) mode.
[[nodiscard]] ProgramHandle loadSlugProgram();

// Skybox program — vec3 position stream (a unit cube). Vertex shader strips
// translation from u_view and emits gl_Position with z=w (forced to far),
// fragment shader samples a TextureCube by the cube's local-space position.
// Used by SkyboxRenderer. Returns kInvalidProgram in headless mode.
[[nodiscard]] ProgramHandle loadSkyboxProgram();

// Rounded-rect program — sprite-style vertex stream (pos2 + uv2 + color4u8)
// plus an extra vec4 TEXCOORD1 attribute carrying (halfWidth, halfHeight,
// cornerRadius, _pad) in screen pixels. Fragment shader runs the rounded-
// box SDF and uses fwidth() to derive an antialiased coverage mask. Used
// by UiRenderer when a rect command has cornerRadius > 0.
[[nodiscard]] ProgramHandle loadRoundedRectProgram();

// Outline fill program — position-only vertex shader, dummy fragment shader.
// Used by the editor selection-outline pass to write a constant value into
// the stencil buffer at every pixel covered by the visible mesh surface.
// The accompanying draw call is configured with color writes disabled so
// only the stencil side-effect is observable.
[[nodiscard]] ProgramHandle loadOutlineFillProgram();

// Outline program — position + oct-encoded normal vertex stream, solid-colour
// fragment shader.  The vertex shader inflates each vertex along its normal
// by u_outlineParams.x metres so that drawing it with stencil_test =
// NOT_EQUAL 1 produces a silhouette band around the original mesh.  Used by
// the editor selection-outline pass.
[[nodiscard]] ProgramHandle loadOutlineProgram();

}  // namespace engine::rendering
