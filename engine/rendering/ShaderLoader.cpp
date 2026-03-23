#include "engine/rendering/ShaderLoader.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

// Generated shader bytecode headers (produced by shaderc via CMake custom commands).
// Each header defines a static uint8_t array named <shader>_<backend>.
// The BGFX_EMBEDDED_SHADER macro concatenates the base name with the backend suffix
// (_mtl, _spv, _glsl, _essl, etc.) to populate the EmbeddedShader table automatically.
// All platform-supported variants must be present — the macro is conditioned on
// BGFX_PLATFORM_SUPPORTS_* flags which are true for multiple backends on macOS.
#include "generated/shaders/fs_unlit_essl.bin.h"
#include "generated/shaders/fs_unlit_glsl.bin.h"
#include "generated/shaders/fs_unlit_mtl.bin.h"
#include "generated/shaders/fs_unlit_spv.bin.h"
#include "generated/shaders/vs_unlit_essl.bin.h"
#include "generated/shaders/vs_unlit_glsl.bin.h"
#include "generated/shaders/vs_unlit_mtl.bin.h"
#include "generated/shaders/vs_unlit_spv.bin.h"

namespace engine::rendering
{

namespace
{

// BGFX_EMBEDDED_SHADER(vs_unlit) looks up symbols named vs_unlit_mtl, vs_unlit_spv, etc.
// which are defined in the included headers above.
static const bgfx::EmbeddedShader kUnlitShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_unlit),
    BGFX_EMBEDDED_SHADER(fs_unlit),
    BGFX_EMBEDDED_SHADER_END(),
};

}  // anonymous namespace

bgfx::ProgramHandle loadUnlitProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    // The Noop renderer (headless unit tests) cannot run real shaders.
    // Return an invalid handle — callers guard with bgfx::isValid().
    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kUnlitShaders, renderer, "vs_unlit");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kUnlitShaders, renderer, "fs_unlit");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    // destroyShaders=true: bgfx takes ownership of vsh/fsh handles.
    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

}  // namespace engine::rendering
