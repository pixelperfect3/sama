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

}  // namespace engine::rendering
