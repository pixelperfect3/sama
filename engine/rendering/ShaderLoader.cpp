#include "engine/rendering/ShaderLoader.h"

#include <bgfx/bgfx.h>

#ifdef __ANDROID__
// On Android, shaders are loaded at runtime from pre-compiled SPIRV binaries
// stored in the APK's assets/shaders/spirv/ folder.

#include <android/asset_manager.h>
#include <android/log.h>

#include "engine/platform/android/AndroidGlobals.h"

#define SHADER_LOG(...) __android_log_print(ANDROID_LOG_INFO, "SamaEngine", __VA_ARGS__)

namespace engine::rendering
{

namespace
{

bgfx::ShaderHandle loadShaderFromAsset(const char* path)
{
    AAssetManager* am = engine::platform::getAssetManager();
    if (!am)
    {
        SHADER_LOG("loadShader: no AAssetManager for %s", path);
        return BGFX_INVALID_HANDLE;
    }

    AAsset* asset = AAssetManager_open(am, path, AASSET_MODE_BUFFER);
    if (!asset)
    {
        SHADER_LOG("loadShader: asset not found: %s", path);
        return BGFX_INVALID_HANDLE;
    }

    size_t size = AAsset_getLength(asset);
    const bgfx::Memory* mem = bgfx::copy(AAsset_getBuffer(asset), static_cast<uint32_t>(size));
    AAsset_close(asset);

    SHADER_LOG("loadShader: loaded %s (%zu bytes)", path, size);
    bgfx::ShaderHandle h = bgfx::createShader(mem);
    SHADER_LOG("loadShader: createShader -> %u (valid=%d)", h.idx, bgfx::isValid(h));
    return h;
}

bgfx::ProgramHandle loadProgramFromAssets(const char* vsPath, const char* fsPath)
{
    bgfx::ShaderHandle vsh = loadShaderFromAsset(vsPath);
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = loadShaderFromAsset(fsPath);
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

}  // anonymous namespace

bgfx::ProgramHandle loadSpriteProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_sprite.bin", "shaders/spirv/fs_sprite.bin");
}

bgfx::ProgramHandle loadRoundedRectProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_rounded_rect.bin",
                                 "shaders/spirv/fs_rounded_rect.bin");
}

bgfx::ProgramHandle loadPbrProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_pbr.bin", "shaders/spirv/fs_pbr.bin");
}

bgfx::ProgramHandle loadUnlitProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_unlit.bin", "shaders/spirv/fs_unlit.bin");
}

bgfx::ProgramHandle loadShadowProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_shadow.bin", "shaders/spirv/fs_shadow.bin");
}

bgfx::ProgramHandle loadSkinnedPbrProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_pbr_skinned.bin", "shaders/spirv/fs_pbr.bin");
}

bgfx::ProgramHandle loadSkinnedShadowProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_shadow_skinned.bin",
                                 "shaders/spirv/fs_shadow.bin");
}

bgfx::ProgramHandle loadGizmoProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_gizmo.bin", "shaders/spirv/fs_gizmo.bin");
}

bgfx::ProgramHandle loadMsdfProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_sprite.bin", "shaders/spirv/fs_msdf.bin");
}

bgfx::ProgramHandle loadSkyboxProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_skybox.bin", "shaders/spirv/fs_skybox.bin");
}

bgfx::ProgramHandle loadSlugProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_slug.bin", "shaders/spirv/fs_slug.bin");
}

bgfx::ProgramHandle loadPostProcessProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_postprocess.bin",
                                 "shaders/spirv/fs_postprocess.bin");
}

bgfx::ProgramHandle loadSsaoProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_ssao.bin", "shaders/spirv/fs_ssao.bin");
}

bgfx::ProgramHandle loadBlurProgram()
{
    return loadProgramFromAssets("shaders/spirv/vs_blur.bin", "shaders/spirv/fs_blur.bin");
}

}  // namespace engine::rendering

#else  // Desktop — embedded shaders from generated headers

#include <bgfx/embedded_shader.h>

// Generated shader bytecode headers (produced by shaderc via CMake custom commands).
#include "generated/shaders/fs_gizmo_essl.bin.h"
#include "generated/shaders/fs_gizmo_glsl.bin.h"
#include "generated/shaders/fs_gizmo_mtl.bin.h"
#include "generated/shaders/fs_gizmo_spv.bin.h"
#include "generated/shaders/fs_msdf_essl.bin.h"
#include "generated/shaders/fs_msdf_glsl.bin.h"
#include "generated/shaders/fs_msdf_mtl.bin.h"
#include "generated/shaders/fs_msdf_spv.bin.h"
#include "generated/shaders/fs_pbr_essl.bin.h"
#include "generated/shaders/fs_pbr_glsl.bin.h"
#include "generated/shaders/fs_pbr_mtl.bin.h"
#include "generated/shaders/fs_pbr_spv.bin.h"
#include "generated/shaders/fs_rounded_rect_essl.bin.h"
#include "generated/shaders/fs_rounded_rect_glsl.bin.h"
#include "generated/shaders/fs_rounded_rect_mtl.bin.h"
#include "generated/shaders/fs_rounded_rect_spv.bin.h"
#include "generated/shaders/fs_shadow_essl.bin.h"
#include "generated/shaders/fs_shadow_glsl.bin.h"
#include "generated/shaders/fs_shadow_mtl.bin.h"
#include "generated/shaders/fs_shadow_spv.bin.h"
#include "generated/shaders/fs_skybox_essl.bin.h"
#include "generated/shaders/fs_skybox_glsl.bin.h"
#include "generated/shaders/fs_skybox_mtl.bin.h"
#include "generated/shaders/fs_skybox_spv.bin.h"
#include "generated/shaders/fs_slug_essl.bin.h"
#include "generated/shaders/fs_slug_glsl.bin.h"
#include "generated/shaders/fs_slug_mtl.bin.h"
#include "generated/shaders/fs_slug_spv.bin.h"
#include "generated/shaders/fs_sprite_essl.bin.h"
#include "generated/shaders/fs_sprite_glsl.bin.h"
#include "generated/shaders/fs_sprite_mtl.bin.h"
#include "generated/shaders/fs_sprite_spv.bin.h"
#include "generated/shaders/fs_unlit_essl.bin.h"
#include "generated/shaders/fs_unlit_glsl.bin.h"
#include "generated/shaders/fs_unlit_mtl.bin.h"
#include "generated/shaders/fs_unlit_spv.bin.h"
#include "generated/shaders/vs_gizmo_essl.bin.h"
#include "generated/shaders/vs_gizmo_glsl.bin.h"
#include "generated/shaders/vs_gizmo_mtl.bin.h"
#include "generated/shaders/vs_gizmo_spv.bin.h"
#include "generated/shaders/vs_pbr_essl.bin.h"
#include "generated/shaders/vs_pbr_glsl.bin.h"
#include "generated/shaders/vs_pbr_mtl.bin.h"
#include "generated/shaders/vs_pbr_skinned_essl.bin.h"
#include "generated/shaders/vs_pbr_skinned_glsl.bin.h"
#include "generated/shaders/vs_pbr_skinned_mtl.bin.h"
#include "generated/shaders/vs_pbr_skinned_spv.bin.h"
#include "generated/shaders/vs_pbr_spv.bin.h"
#include "generated/shaders/vs_rounded_rect_essl.bin.h"
#include "generated/shaders/vs_rounded_rect_glsl.bin.h"
#include "generated/shaders/vs_rounded_rect_mtl.bin.h"
#include "generated/shaders/vs_rounded_rect_spv.bin.h"
#include "generated/shaders/vs_shadow_essl.bin.h"
#include "generated/shaders/vs_shadow_glsl.bin.h"
#include "generated/shaders/vs_shadow_mtl.bin.h"
#include "generated/shaders/vs_shadow_skinned_essl.bin.h"
#include "generated/shaders/vs_shadow_skinned_glsl.bin.h"
#include "generated/shaders/vs_shadow_skinned_mtl.bin.h"
#include "generated/shaders/vs_shadow_skinned_spv.bin.h"
#include "generated/shaders/vs_shadow_spv.bin.h"
#include "generated/shaders/vs_skybox_essl.bin.h"
#include "generated/shaders/vs_skybox_glsl.bin.h"
#include "generated/shaders/vs_skybox_mtl.bin.h"
#include "generated/shaders/vs_skybox_spv.bin.h"
#include "generated/shaders/vs_slug_essl.bin.h"
#include "generated/shaders/vs_slug_glsl.bin.h"
#include "generated/shaders/vs_slug_mtl.bin.h"
#include "generated/shaders/vs_slug_spv.bin.h"
#include "generated/shaders/vs_sprite_essl.bin.h"
#include "generated/shaders/vs_sprite_glsl.bin.h"
#include "generated/shaders/vs_sprite_mtl.bin.h"
#include "generated/shaders/vs_sprite_spv.bin.h"
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

static const bgfx::EmbeddedShader kSkinnedShadowShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_shadow_skinned),
    BGFX_EMBEDDED_SHADER(fs_shadow),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kGizmoShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_gizmo),
    BGFX_EMBEDDED_SHADER(fs_gizmo),
    BGFX_EMBEDDED_SHADER_END(),
};

// MSDF text — reuses the sprite vertex shader, pairs with fs_msdf.
static const bgfx::EmbeddedShader kMsdfShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_sprite),
    BGFX_EMBEDDED_SHADER(fs_msdf),
    BGFX_EMBEDDED_SHADER_END(),
};

// Slug text — vector glyphs from packed Bezier curves; own vs + fs.
static const bgfx::EmbeddedShader kSlugShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_slug),
    BGFX_EMBEDDED_SHADER(fs_slug),
    BGFX_EMBEDDED_SHADER_END(),
};

// Skybox — unit cube + cubemap fragment sampler.
static const bgfx::EmbeddedShader kSkyboxShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_skybox),
    BGFX_EMBEDDED_SHADER(fs_skybox),
    BGFX_EMBEDDED_SHADER_END(),
};

// Rounded rect — own vs + fs because the vertex layout has an extra
// TEXCOORD1 vec4 carrying per-rect SDF data.
static const bgfx::EmbeddedShader kRoundedRectShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_rounded_rect),
    BGFX_EMBEDDED_SHADER(fs_rounded_rect),
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

bgfx::ProgramHandle loadMsdfProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    // The Noop renderer (headless unit tests) cannot run real shaders.
    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kMsdfShaders, renderer, "vs_sprite");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kMsdfShaders, renderer, "fs_msdf");
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

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kSkinnedPbrShaders, renderer, "fs_pbr");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadSkinnedShadowProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh =
        bgfx::createEmbeddedShader(kSkinnedShadowShaders, renderer, "vs_shadow_skinned");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh =
        bgfx::createEmbeddedShader(kSkinnedShadowShaders, renderer, "fs_shadow");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadGizmoProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kGizmoShaders, renderer, "vs_gizmo");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kGizmoShaders, renderer, "fs_gizmo");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadSlugProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kSlugShaders, renderer, "vs_slug");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kSlugShaders, renderer, "fs_slug");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadSkyboxProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kSkyboxShaders, renderer, "vs_skybox");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kSkyboxShaders, renderer, "fs_skybox");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

bgfx::ProgramHandle loadRoundedRectProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();

    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh =
        bgfx::createEmbeddedShader(kRoundedRectShaders, renderer, "vs_rounded_rect");
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh =
        bgfx::createEmbeddedShader(kRoundedRectShaders, renderer, "fs_rounded_rect");
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}

}  // namespace engine::rendering

#endif  // __ANDROID__
