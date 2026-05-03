#include "engine/rendering/systems/SsaoSystem.h"

#if defined(__APPLE__)
// TargetConditionals is Apple-only — guards the iOS branch below.  Including
// it unconditionally breaks the Android NDK build (no such header in Bionic).
#include <TargetConditionals.h>
#endif
#include <bgfx/bgfx.h>

#include <cmath>

#ifdef __ANDROID__
namespace engine::rendering
{
bgfx::ProgramHandle loadSsaoProgram();
}  // namespace engine::rendering
#else
#include <bgfx/embedded_shader.h>

// Generated shader bytecode headers — produced by shaderc via CMake.
// iOS only ships the Metal variants (BGFX_PLATFORM_SUPPORTS_{ESSL,GLSL,SPIRV}=0
// on iOS so the BGFX_EMBEDDED_SHADER macro skips them); other Apple targets
// + Linux + Windows still embed all four backends so a single binary can
// target any GPU bgfx picks.
#include "generated/shaders/fs_ssao_mtl.bin.h"
#include "generated/shaders/vs_fullscreen_mtl.bin.h"
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
#include "generated/shaders/fs_ssao_essl.bin.h"
#include "generated/shaders/fs_ssao_glsl.bin.h"
#include "generated/shaders/fs_ssao_spv.bin.h"
#include "generated/shaders/vs_fullscreen_essl.bin.h"
#include "generated/shaders/vs_fullscreen_glsl.bin.h"
#include "generated/shaders/vs_fullscreen_spv.bin.h"
#endif
#endif

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Embedded shader table (desktop only — Android uses asset-loaded programs)
// ---------------------------------------------------------------------------

namespace
{

#ifndef __ANDROID__
static const bgfx::EmbeddedShader kSsaoShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_ssao),
    BGFX_EMBEDDED_SHADER_END(),
};

// Load a program from the embedded shader table.
// Returns BGFX_INVALID_HANDLE in headless (Noop) mode or on failure.
bgfx::ProgramHandle loadEmbeddedProgram(const bgfx::EmbeddedShader* shaders, const char* vsName,
                                        const char* fsName)
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    if (renderer == bgfx::RendererType::Noop)
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(shaders, renderer, vsName);
    if (!bgfx::isValid(vsh))
        return BGFX_INVALID_HANDLE;

    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(shaders, renderer, fsName);
    if (!bgfx::isValid(fsh))
    {
        bgfx::destroy(vsh);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vsh, fsh, /*destroyShaders=*/true);
}
#endif  // __ANDROID__

// Pseudo-random float in [0, 1) using a simple LCG.  Fixed seed for
// deterministic kernel generation across all platforms.
float lcgRand(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void SsaoSystem::init(uint16_t w, uint16_t h)
{
    if (w == width_ && h == height_ && bgfx::isValid(ssaoMap_))
        return;

    shutdown();

    width_ = w;
    height_ = h;

    // -----------------------------------------------------------------
    // R8 SSAO output texture + single-attachment framebuffer
    // -----------------------------------------------------------------
    ssaoMap_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::R8, BGFX_TEXTURE_RT);

    ssaoFb_ = bgfx::createFrameBuffer(1, &ssaoMap_, /*destroyTextures=*/false);

    // -----------------------------------------------------------------
    // Hemisphere kernel — 16 samples, z > 0, length in (0, 1]
    // Use a fixed seed (42) so the kernel is identical across runs.
    // Samples are distributed with an accelerating distance function so
    // more samples cluster close to the origin.
    // -----------------------------------------------------------------
    uint32_t rngState = 42u;
    for (int i = 0; i < 16; ++i)
    {
        // Random direction on the unit hemisphere (z >= 0).
        float x = lcgRand(rngState) * 2.0f - 1.0f;
        float y = lcgRand(rngState) * 2.0f - 1.0f;
        float z = lcgRand(rngState);  // [0, 1) — keeps z > 0

        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-6f)
            len = 1e-6f;
        x /= len;
        y /= len;
        z /= len;

        // Scale: accelerating distance so most samples are near the origin.
        float scale = static_cast<float>(i) / 16.0f;
        scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, t^2)

        kernel_[i][0] = x * scale;
        kernel_[i][1] = y * scale;
        kernel_[i][2] = z * scale;
        kernel_[i][3] = 0.0f;
    }

    // -----------------------------------------------------------------
    // SSAO shader program
    // -----------------------------------------------------------------
#ifdef __ANDROID__
    program_ = loadSsaoProgram();
#else
    program_ = loadEmbeddedProgram(kSsaoShaders, "vs_fullscreen", "fs_ssao");
#endif
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void SsaoSystem::shutdown()
{
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }

    if (bgfx::isValid(ssaoFb_))
    {
        bgfx::destroy(ssaoFb_);
        ssaoFb_ = BGFX_INVALID_HANDLE;
    }

    if (bgfx::isValid(ssaoMap_))
    {
        bgfx::destroy(ssaoMap_);
        ssaoMap_ = BGFX_INVALID_HANDLE;
    }

    width_ = 0;
    height_ = 0;
}

// ---------------------------------------------------------------------------
// submit
// ---------------------------------------------------------------------------

void SsaoSystem::submit(const SsaoSettings& settings, const ShaderUniforms& uniforms,
                        bgfx::TextureHandle depthTex, bgfx::ViewId viewId,
                        bgfx::VertexBufferHandle fsTriVb)
{
    // In headless (Noop) mode none of the handles are valid — skip gracefully.
    if (!bgfx::isValid(program_) || !bgfx::isValid(ssaoFb_) || !bgfx::isValid(fsTriVb))
        return;

    // Upload the kernel — 16 × vec4.
    bgfx::setUniform(uniforms.u_ssaoKernel, kernel_, 16);

    // Upload SSAO params: {radius, bias, power=2.0, sampleCount}
    const float ssaoParamsData[4] = {
        settings.radius,
        settings.bias,
        2.0f,  // power exponent — hardens the occlusion curve
        static_cast<float>(settings.sampleCount),
    };
    bgfx::setUniform(uniforms.u_ssaoParams, ssaoParamsData);

    // Bind the depth texture to slot 9 (s_depth).
    bgfx::setTexture(9, uniforms.s_depth, depthTex);

    // Configure the view.
    bgfx::setViewFrameBuffer(viewId, ssaoFb_);
    bgfx::setViewRect(viewId, 0, 0, width_, height_);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);

    bgfx::setVertexBuffer(0, fsTriVb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, program_);
}

}  // namespace engine::rendering
