#include "engine/rendering/ShaderLoader.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

// Generated shader bytecode headers (produced by shaderc via CMake custom commands).
// Each header defines a static uint8_t array named <shader>_<backend>.
// The BGFX_EMBEDDED_SHADER macro concatenates the base name with the backend suffix
// (_mtl, _spv, _glsl, _essl, etc.) to populate the EmbeddedShader table automatically.
// All platform-supported variants must be present — the macro is conditioned on
// BGFX_PLATFORM_SUPPORTS_* flags which are true for multiple backends on macOS.
#include "generated/shaders/fs_pbr_essl.bin.h"
#include "generated/shaders/fs_pbr_glsl.bin.h"
#include "generated/shaders/fs_pbr_mtl.bin.h"
#include "generated/shaders/fs_pbr_spv.bin.h"
#include "generated/shaders/fs_shadow_essl.bin.h"
#include "generated/shaders/fs_shadow_glsl.bin.h"
#include "generated/shaders/fs_shadow_mtl.bin.h"
#include "generated/shaders/fs_shadow_spv.bin.h"
#include "generated/shaders/fs_sprite_essl.bin.h"
#include "generated/shaders/fs_sprite_glsl.bin.h"
#include "generated/shaders/fs_sprite_mtl.bin.h"
#include "generated/shaders/fs_sprite_spv.bin.h"
#include "generated/shaders/fs_unlit_essl.bin.h"
#include "generated/shaders/fs_unlit_glsl.bin.h"
#include "generated/shaders/fs_unlit_mtl.bin.h"
#include "generated/shaders/fs_unlit_spv.bin.h"
#include "generated/shaders/vs_pbr_essl.bin.h"
#include "generated/shaders/vs_pbr_glsl.bin.h"
#include "generated/shaders/vs_pbr_mtl.bin.h"
#include "generated/shaders/vs_pbr_spv.bin.h"
#include "generated/shaders/vs_shadow_essl.bin.h"
#include "generated/shaders/vs_shadow_glsl.bin.h"
#include "generated/shaders/vs_shadow_mtl.bin.h"
#include "generated/shaders/vs_shadow_spv.bin.h"
#include "generated/shaders/vs_sprite_essl.bin.h"
#include "generated/shaders/vs_sprite_glsl.bin.h"
#include "generated/shaders/vs_sprite_mtl.bin.h"
#include "generated/shaders/vs_sprite_spv.bin.h"
#include "generated/shaders/vs_pbr_skinned_essl.bin.h"
#include "generated/shaders/vs_pbr_skinned_glsl.bin.h"
#include "generated/shaders/vs_pbr_skinned_mtl.bin.h"
#include "generated/shaders/vs_pbr_skinned_spv.bin.h"
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

static const bgfx::EmbeddedShader kPbrShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_pbr),
    BGFX_EMBEDDED_SHADER(fs_pbr),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kSpriteShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_sprite),
    BGFX_EMBEDDED_SHADER(fs_sprite),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kShadowShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_shadow),
    BGFX_EMBEDDED_SHADER(fs_shadow),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kSkinnedPbrShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_pbr_skinned),
    BGFX_EMBEDDED_SHADER(fs_pbr),
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

bgfx::ProgramHandle loadPbrProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    // The Noop renderer (headless unit tests) cannot run real shaders.
    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kPbrShaders, renderer, "vs_pbr");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kPbrShaders, renderer, "fs_pbr");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadSpriteProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    // The Noop renderer (headless unit tests) cannot run real shaders.
    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kSpriteShaders, renderer, "vs_sprite");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kSpriteShaders, renderer, "fs_sprite");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadShadowProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    // The Noop renderer (headless unit tests) cannot run real shaders.
    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kShadowShaders, renderer, "vs_shadow");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kShadowShaders, renderer, "fs_shadow");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadSkinnedPbrProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh =
        bgfx::createEmbeddedShader(kSkinnedPbrShaders, renderer, "vs_pbr_skinned");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh =
        bgfx::createEmbeddedShader(kSkinnedPbrShaders, renderer, "fs_pbr");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

}  // namespace engine::rendering
