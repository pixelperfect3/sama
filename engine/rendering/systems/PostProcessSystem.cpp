#include "engine/rendering/systems/PostProcessSystem.h"

#include <bgfx/bgfx.h>

#ifdef __ANDROID__
// On Android, post-process programs are loaded from pre-compiled SPIRV
// shipped in the APK (see android/compile_shaders.sh). The declarations
// below shadow forward-only entry points exposed by ShaderLoader.cpp.
namespace engine::rendering
{
bgfx::ProgramHandle loadBloomThresholdProgram();
bgfx::ProgramHandle loadBloomDownsampleProgram();
bgfx::ProgramHandle loadBloomUpsampleProgram();
bgfx::ProgramHandle loadTonemapProgram();
bgfx::ProgramHandle loadFxaaProgram();
}  // namespace engine::rendering
#else
#include <bgfx/embedded_shader.h>

// Generated shader bytecode headers — produced by shaderc via CMake custom commands.
#include "generated/shaders/fs_bloom_downsample_essl.bin.h"
#include "generated/shaders/fs_bloom_downsample_glsl.bin.h"
#include "generated/shaders/fs_bloom_downsample_mtl.bin.h"
#include "generated/shaders/fs_bloom_downsample_spv.bin.h"
#include "generated/shaders/fs_bloom_threshold_essl.bin.h"
#include "generated/shaders/fs_bloom_threshold_glsl.bin.h"
#include "generated/shaders/fs_bloom_threshold_mtl.bin.h"
#include "generated/shaders/fs_bloom_threshold_spv.bin.h"
#include "generated/shaders/fs_bloom_upsample_essl.bin.h"
#include "generated/shaders/fs_bloom_upsample_glsl.bin.h"
#include "generated/shaders/fs_bloom_upsample_mtl.bin.h"
#include "generated/shaders/fs_bloom_upsample_spv.bin.h"
#include "generated/shaders/fs_fxaa_essl.bin.h"
#include "generated/shaders/fs_fxaa_glsl.bin.h"
#include "generated/shaders/fs_fxaa_mtl.bin.h"
#include "generated/shaders/fs_fxaa_spv.bin.h"
#include "generated/shaders/fs_tonemap_essl.bin.h"
#include "generated/shaders/fs_tonemap_glsl.bin.h"
#include "generated/shaders/fs_tonemap_mtl.bin.h"
#include "generated/shaders/fs_tonemap_spv.bin.h"
#include "generated/shaders/vs_fullscreen_essl.bin.h"
#include "generated/shaders/vs_fullscreen_glsl.bin.h"
#include "generated/shaders/vs_fullscreen_mtl.bin.h"
#include "generated/shaders/vs_fullscreen_spv.bin.h"
#endif

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Embedded shader tables (desktop only — Android uses asset-loaded programs)
// ---------------------------------------------------------------------------

namespace
{

#ifndef __ANDROID__
static const bgfx::EmbeddedShader kBloomThreshShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_bloom_threshold),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kBloomDownShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_bloom_downsample),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kBloomUpShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_bloom_upsample),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kTonemapShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_tonemap),
    BGFX_EMBEDDED_SHADER_END(),
};

static const bgfx::EmbeddedShader kFxaaShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_fxaa),
    BGFX_EMBEDDED_SHADER_END(),
};

// Helper: load a program from an embedded shader table.
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

// Submit a full-screen triangle pass using a pre-built vertex buffer.
void submitFullscreenPass(bgfx::ViewId viewId, bgfx::FrameBufferHandle targetFb, uint16_t w,
                          uint16_t h, bgfx::VertexBufferHandle vb, bgfx::ProgramHandle program)
{
    bgfx::setViewFrameBuffer(viewId, targetFb);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setVertexBuffer(0, vb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, program);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// init / shutdown / resize
// ---------------------------------------------------------------------------

bool PostProcessSystem::init(uint16_t w, uint16_t h)
{
    resources_.validate(w, h, /*downsampleSteps=*/5);
    ssaoSystem_.init(w, h);

    // -----------------------------------------------------------------
    // Build the fullscreen triangle vertex buffer (clip-space positions).
    // Three vertices forming a triangle that covers the entire NDC square:
    //   (-1,-1)  →  UV (0,1)
    //   ( 3,-1)  →  UV (2,1)
    //   (-1, 3)  →  UV (0,-1)
    // The vertex shader derives UV from position, so we only need XY.
    // -----------------------------------------------------------------
    fsTriLayout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();

    // Store XYZ (Z=0) for each of the three triangle vertices.
    static const float kFsTriVerts[9] = {
        -1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f,
    };

    const bgfx::Memory* mem = bgfx::makeRef(kFsTriVerts, sizeof(kFsTriVerts));
    fsTriVb_ = bgfx::createVertexBuffer(mem, fsTriLayout_);

#ifdef __ANDROID__
    bloomThreshProgram_ = loadBloomThresholdProgram();
    bloomDownProgram_ = loadBloomDownsampleProgram();
    bloomUpProgram_ = loadBloomUpsampleProgram();
    tonemapProgram_ = loadTonemapProgram();
    fxaaProgram_ = loadFxaaProgram();
#else
    bloomThreshProgram_ =
        loadEmbeddedProgram(kBloomThreshShaders, "vs_fullscreen", "fs_bloom_threshold");
    bloomDownProgram_ =
        loadEmbeddedProgram(kBloomDownShaders, "vs_fullscreen", "fs_bloom_downsample");
    bloomUpProgram_ = loadEmbeddedProgram(kBloomUpShaders, "vs_fullscreen", "fs_bloom_upsample");
    tonemapProgram_ = loadEmbeddedProgram(kTonemapShaders, "vs_fullscreen", "fs_tonemap");
    fxaaProgram_ = loadEmbeddedProgram(kFxaaShaders, "vs_fullscreen", "fs_fxaa");
#endif

    // Programs will be BGFX_INVALID_HANDLE in headless (Noop) mode; that is
    // expected — submit() guards each bgfx::submit call with isValid().
    return true;
}

void PostProcessSystem::shutdown()
{
    auto safeDestroyProgram = [](bgfx::ProgramHandle& h)
    {
        if (bgfx::isValid(h))
        {
            bgfx::destroy(h);
            h = BGFX_INVALID_HANDLE;
        }
    };

    safeDestroyProgram(bloomThreshProgram_);
    safeDestroyProgram(bloomDownProgram_);
    safeDestroyProgram(bloomUpProgram_);
    safeDestroyProgram(tonemapProgram_);
    safeDestroyProgram(fxaaProgram_);

    ssaoSystem_.shutdown();

    if (bgfx::isValid(fsTriVb_))
    {
        bgfx::destroy(fsTriVb_);
        fsTriVb_ = BGFX_INVALID_HANDLE;
    }

    resources_.shutdown();
}

void PostProcessSystem::resize(uint16_t w, uint16_t h)
{
    resources_.validate(w, h, resources_.steps());
    ssaoSystem_.init(w, h);
}

// ---------------------------------------------------------------------------
// submit
// ---------------------------------------------------------------------------

bgfx::ViewId PostProcessSystem::submit(const PostProcessSettings& settings,
                                       const ShaderUniforms& uniforms, bgfx::ViewId firstViewId)
{
    bgfx::ViewId viewId = firstViewId;
    const uint16_t w = resources_.width();
    const uint16_t h = resources_.height();
    const uint8_t steps = resources_.steps();

    // ------------------------------------------------------------------
    // SSAO pass (optional) — runs first, before bloom, using the scene depth.
    // Writes occlusion to ssaoSystem_.ssaoMap(); the PBR shader samples this.
    // ------------------------------------------------------------------
    if (settings.ssao.enabled)
    {
        ssaoSystem_.submit(settings.ssao, uniforms, resources_.sceneDepth(), viewId, fsTriVb_);
        ++viewId;
    }

    // Bloom params reused across multiple passes — set once per draw call below.
    const float bloomParamsData[4] = {
        settings.bloom.threshold,
        settings.bloom.intensity,
        0.0f,
        0.0f,
    };

    if (settings.bloom.enabled && steps > 0)
    {
        // ------------------------------------------------------------------
        // Pass 0: Bloom threshold — hdrColor → bloomLevel[0] (full res)
        // ------------------------------------------------------------------
        {
            const float texelSizeData[4] = {
                1.0f / static_cast<float>(w),
                1.0f / static_cast<float>(h),
                0.0f,
                0.0f,
            };
            bgfx::setUniform(uniforms.u_texelSize, texelSizeData);
            bgfx::setUniform(uniforms.u_bloomParams, bloomParamsData);
            bgfx::setTexture(0, uniforms.s_hdrColor, resources_.hdrColor());

            submitFullscreenPass(viewId, resources_.bloomLevelFb(0), w, h, fsTriVb_,
                                 bloomThreshProgram_);
            ++viewId;
        }

        // ------------------------------------------------------------------
        // Passes 1..steps-1: Bloom downsample
        // ------------------------------------------------------------------
        for (uint8_t i = 1; i < steps; ++i)
        {
            const uint16_t srcW = w >> (i - 1);
            const uint16_t srcH = h >> (i - 1);
            const uint16_t dstW = w >> i;
            const uint16_t dstH = h >> i;

            const float texelSizeData[4] = {
                1.0f / static_cast<float>(srcW > 0 ? srcW : 1),
                1.0f / static_cast<float>(srcH > 0 ? srcH : 1),
                0.0f,
                0.0f,
            };
            bgfx::setUniform(uniforms.u_texelSize, texelSizeData);
            bgfx::setTexture(0, uniforms.s_hdrColor, resources_.bloomLevel(i - 1));

            submitFullscreenPass(viewId, resources_.bloomLevelFb(i), dstW > 0 ? dstW : 1,
                                 dstH > 0 ? dstH : 1, fsTriVb_, bloomDownProgram_);
            ++viewId;
        }

        // ------------------------------------------------------------------
        // Upsample passes: steps-1 down to 1 (writes into the coarser level)
        // ------------------------------------------------------------------
        for (int8_t i = static_cast<int8_t>(steps) - 1; i >= 1; --i)
        {
            const uint16_t srcW = w >> i;
            const uint16_t srcH = h >> i;
            const uint16_t dstW = w >> (i - 1);
            const uint16_t dstH = h >> (i - 1);

            const float texelSizeData[4] = {
                1.0f / static_cast<float>(srcW > 0 ? srcW : 1),
                1.0f / static_cast<float>(srcH > 0 ? srcH : 1),
                0.0f,
                0.0f,
            };
            bgfx::setUniform(uniforms.u_texelSize, texelSizeData);
            bgfx::setUniform(uniforms.u_bloomParams, bloomParamsData);
            bgfx::setTexture(0, uniforms.s_hdrColor, resources_.bloomLevel(i));
            bgfx::setTexture(1, uniforms.s_bloomPrev, resources_.bloomLevel(i - 1));

            submitFullscreenPass(viewId, resources_.bloomLevelFb(i - 1), dstW > 0 ? dstW : 1,
                                 dstH > 0 ? dstH : 1, fsTriVb_, bloomUpProgram_);
            ++viewId;
        }
    }

    // ------------------------------------------------------------------
    // Tonemap pass — hdrColor + bloomLevel[0] (if bloom) → ldrFb (or backbuffer)
    // ------------------------------------------------------------------
    {
        const bool needFxaa = settings.fxaaEnabled;
        bgfx::FrameBufferHandle tonemapTarget =
            needFxaa ? resources_.ldrFb() : bgfx::FrameBufferHandle{bgfx::kInvalidHandle};

        bgfx::setTexture(0, uniforms.s_hdrColor, resources_.hdrColor());

        if (settings.bloom.enabled && steps > 0)
        {
            bgfx::setUniform(uniforms.u_bloomParams, bloomParamsData);
            bgfx::setTexture(1, uniforms.s_bloomTex, resources_.bloomLevel(0));
        }
        else
        {
            // Point the bloom sampler at hdrColor with intensity=0 so the
            // shader's bloom contribution is zero (threshold pass was skipped).
            float zeroBloom[4] = {settings.bloom.threshold, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(uniforms.u_bloomParams, zeroBloom);
            bgfx::setTexture(1, uniforms.s_bloomTex, resources_.hdrColor());
        }

        bgfx::setViewFrameBuffer(viewId, tonemapTarget);
        bgfx::setViewRect(viewId, 0, 0, w, h);
        bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
        bgfx::setVertexBuffer(0, fsTriVb_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(viewId, tonemapProgram_);
        ++viewId;
    }

    // ------------------------------------------------------------------
    // FXAA pass — ldrColor → backbuffer (BGFX_INVALID_HANDLE)
    // ------------------------------------------------------------------
    if (settings.fxaaEnabled)
    {
        const float texelSizeData[4] = {
            1.0f / static_cast<float>(w),
            1.0f / static_cast<float>(h),
            0.0f,
            0.0f,
        };
        bgfx::setUniform(uniforms.u_texelSize, texelSizeData);
        bgfx::setTexture(0, uniforms.s_ldrColor, resources_.ldrColor());

        // Final pass always targets the backbuffer.
        bgfx::setViewFrameBuffer(viewId, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(viewId, 0, 0, w, h);
        bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
        bgfx::setVertexBuffer(0, fsTriVb_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(viewId, fxaaProgram_);
        ++viewId;
    }

    return viewId;
}

}  // namespace engine::rendering
