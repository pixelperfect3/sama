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

}  // namespace engine::rendering
