#pragma once

#include <bgfx/bgfx.h>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// ShaderLoader — creates bgfx shader programs from embedded compiled bytecode.
//
// Shader bytecode is compiled offline by shaderc (via CMake custom commands)
// and embedded as C header arrays.  loadUnlitProgram() selects the correct
// bytecode for the current renderer backend at runtime.
//
// All returned handles are owned by the caller and must be destroyed with
// bgfx::destroy() when no longer needed.
// ---------------------------------------------------------------------------

// Unlit program — position stream only, outputs a solid orange colour.
// Used for Phase 2 smoke tests.  Returns BGFX_INVALID_HANDLE on failure.
[[nodiscard]] bgfx::ProgramHandle loadUnlitProgram();

// PBR program — two-stream vertex (position + surface), GGX PBR fragment shader.
// Returns BGFX_INVALID_HANDLE on failure or when running the Noop renderer.
[[nodiscard]] bgfx::ProgramHandle loadPbrProgram();

// Sprite program — position + texcoord0 + color0 streams, samples s_texture
// and multiplies by vertex color.  Used by UiRenderSystem / SpriteBatcher.
// Returns BGFX_INVALID_HANDLE on failure or in headless (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadSpriteProgram();

// MSDF text program — reuses vs_sprite for the vertex stream, samples an
// RGB multi-channel signed distance field atlas via s_texture, and runs
// the MSDF reconstruction fragment shader (median + smoothstep).  Used by
// MsdfFont.  Requires a Vec4 uniform u_msdfParams where x = distanceRange
// in atlas pixels, set per-batch via MsdfFont::bindResources().
// Returns BGFX_INVALID_HANDLE on failure or in headless (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadMsdfProgram();

// Shadow program — depth-only, Stream 0 (position) only.
// Used by the shadow pass to render scene geometry into the shadow atlas.
// Returns BGFX_INVALID_HANDLE on failure or in headless (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadShadowProgram();

// Skinned PBR program — same fragment shader as PBR, but vertex shader
// performs GPU skinning via u_model[] bone matrices + a_indices/a_weight.
// Returns BGFX_INVALID_HANDLE on failure or in headless (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadSkinnedPbrProgram();

// Skinned shadow program — depth-only vertex shader with GPU skinning.
// Used by the shadow pass for skinned meshes (bone matrices via u_model[]).
// Returns BGFX_INVALID_HANDLE on failure or in headless (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadSkinnedShadowProgram();

// Gizmo program — position + color0 passthrough.  Used for colored
// transform gizmo lines/arrows in the editor overlay.
// Returns BGFX_INVALID_HANDLE on failure or in headless (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadGizmoProgram();

// Slug program — position + texcoord0 + color0 streams (same layout as
// sprite). Fragment shader evaluates packed quadratic Bezier curves from
// a buffer texture to produce vector-perfect glyph coverage at any scale.
// Used by SlugFont. Returns BGFX_INVALID_HANDLE on failure or in headless
// (Noop) mode.
[[nodiscard]] bgfx::ProgramHandle loadSlugProgram();

// Skybox program — vec3 position stream (a unit cube). Vertex shader strips
// translation from u_view and emits gl_Position with z=w (forced to far),
// fragment shader samples a TextureCube by the cube's local-space position.
// Used by SkyboxRenderer. Returns BGFX_INVALID_HANDLE in headless mode.
[[nodiscard]] bgfx::ProgramHandle loadSkyboxProgram();

}  // namespace engine::rendering
