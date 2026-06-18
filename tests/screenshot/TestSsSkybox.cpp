// Skybox cubemap regression test — guards the Metal vec3(0) bug.
//
// History: vs_skybox.sc originally extracted the camera world position as
//   vec3 camPos = vec3(u_invView[3][0], u_invView[3][1], u_invView[3][2]);
// which is correct GLSL but bgfx's spirv-cross MSL emitter transcribed the
// access pattern as u_invView[0..2][3] (the bottom row of an affine matrix,
// all zeros) — so on macOS Metal camPos collapsed to vec3(0) and every
// fragment received the same direction, sampling a single cube face for
// the entire viewport.  Fixed by switching to
//   vec3 camPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
// which spirv-cross emits correctly on every backend.
//
// This test renders a procedural cubemap with six distinctly colored faces
// (R / G / B / Y / M / C) through SkyboxRenderer with the camera at
// (3, 2, 4) looking at the origin.  With the bug, the entire viewport
// renders as one solid face color (whichever cube face the camera-position
// vector dominantly points along — magenta for this camera).  With the
// fix, the viewport shows multiple face colors meeting along the cube-face
// boundaries.  Pixel-by-pixel golden compare will diverge dramatically
// between the two cases.
//
// Catches the regression specifically when run on a Metal backend (macOS
// dev / CI).  On other backends both versions produce the same output
// because the shader emit is correct in their respective HLSL/GLSL/SPIRV
// translations; the golden still serves as a regression check for any
// other change to vs_skybox / fs_skybox that would alter the rendered
// output.

#include <bgfx/bgfx.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/SkyboxRenderer.h"
#include "engine/rendering/ViewIds.h"

namespace
{

// Per-face RGBA8 colors, indexed by bgfx cube side (0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z).
// Picked so each face produces a unique post-tonemap output color — no two
// faces tonemap to the same byte triple under Reinhard + 2.2 gamma.
constexpr std::array<std::array<uint8_t, 4>, 6> kFaceColors = {{
    {{255, 0, 0, 255}},    // +X red
    {{0, 255, 0, 255}},    // -X green
    {{0, 0, 255, 255}},    // +Y blue
    {{255, 255, 0, 255}},  // -Y yellow
    {{255, 0, 255, 255}},  // +Z magenta
    {{0, 255, 255, 255}},  // -Z cyan
}};

// Build a small cubemap with each face flood-filled to its color.
// 16×16 is large enough for the sampler to filter cleanly but small enough
// that the upload is trivial.
bgfx::TextureHandle makeTestCubemap()
{
    constexpr uint16_t kSize = 16;
    constexpr uint32_t kFaceBytes = kSize * kSize * 4;

    const bgfx::Memory* mem = bgfx::alloc(kFaceBytes * 6);
    uint8_t* dst = mem->data;

    for (uint8_t face = 0; face < 6; ++face)
    {
        const auto& color = kFaceColors[face];
        for (uint32_t px = 0; px < kSize * kSize; ++px)
        {
            dst[face * kFaceBytes + px * 4 + 0] = color[0];
            dst[face * kFaceBytes + px * 4 + 1] = color[1];
            dst[face * kFaceBytes + px * 4 + 2] = color[2];
            dst[face * kFaceBytes + px * 4 + 3] = color[3];
        }
    }

    return bgfx::createTextureCube(
        kSize, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, mem);
}

}  // namespace

TEST_CASE("screenshot: skybox cubemap per-pixel direction (Metal vec3(0) regression)",
          "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;

    bgfx::TextureHandle cubemap = makeTestCubemap();

    engine::rendering::SkyboxRenderer sky;
    sky.init();

    // Camera at (3, 2, 4) looking at the origin.  Forward direction is
    // -(3,2,4)/|…| ≈ (-0.557, -0.371, -0.743), dominant -Z → centre pixel
    // samples the -Z (cyan) face under the fixed shader.  Under the bug
    // (camPos = vec3(0)) every fragment uses the camera-position vector
    // (3, 2, 4) directly, which normalizes to (+0.557, +0.371, +0.743) —
    // dominant +Z → entire viewport renders magenta.
    auto view = glm::lookAt(glm::vec3(3, 2, 4), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.sceneFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x111111ff)
        .transform(view, proj);

    // SkyboxRenderer wraps the bgfx::TextureHandle in its bgfx-free
    // boundary type; the layout is identical (RenderPass.cpp asserts).
    sky.render(engine::rendering::kViewOpaque, engine::rendering::TextureHandle{cubemap.idx});

    fx.runTonemap(engine::rendering::kViewPostProcessBase);

    auto pixels = fx.captureFrame();

    sky.shutdown();
    if (bgfx::isValid(cubemap))
        bgfx::destroy(cubemap);

    REQUIRE(engine::screenshot::compareOrUpdateGolden("skybox_cubemap_per_pixel_dir", pixels,
                                                      fx.width(), fx.height()));
}
