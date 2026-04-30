#include <catch2/catch_test_macros.hpp>

#include "engine/rendering/PostProcessResources.h"
#include "engine/rendering/RenderSettings.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/systems/PostProcessSystem.h"

// ---------------------------------------------------------------------------
// Shared headless bgfx fixture
// ---------------------------------------------------------------------------

struct HeadlessBgfx
{
    engine::rendering::Renderer renderer;

    HeadlessBgfx()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 1;
        desc.height = 1;
        REQUIRE(renderer.init(desc));
    }

    ~HeadlessBgfx()
    {
        renderer.shutdown();
    }

    HeadlessBgfx(const HeadlessBgfx&) = delete;
    HeadlessBgfx& operator=(const HeadlessBgfx&) = delete;
};

// ---------------------------------------------------------------------------
// PostProcessResources::validate() — no crash in headless mode
// ---------------------------------------------------------------------------

TEST_CASE("PostProcessResources: validate does not crash in headless mode", "[postprocess]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::PostProcessResources res;
    REQUIRE_NOTHROW(res.validate(1280, 720, 5));

    // Calling validate a second time with the same args is a no-op.
    REQUIRE_NOTHROW(res.validate(1280, 720, 5));

    // Resize triggers re-allocation.
    REQUIRE_NOTHROW(res.validate(640, 360, 5));

    REQUIRE_NOTHROW(res.shutdown());
}

// ---------------------------------------------------------------------------
// PostProcessResources — accessor guards
// ---------------------------------------------------------------------------

TEST_CASE("PostProcessResources: out-of-range bloom level returns invalid handle", "[postprocess]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::PostProcessResources res;
    res.validate(1280, 720, 3);  // steps = 3 → levels 0, 1, 2

    // Level 3 is out of range for steps=3.
    REQUIRE(!bgfx::isValid(res.bloomLevel(3)));
    REQUIRE(!bgfx::isValid(res.bloomLevelFb(3)));

    res.shutdown();
}

// ---------------------------------------------------------------------------
// PostProcessSystem::init() — no crash, programs are invalid in headless mode
// ---------------------------------------------------------------------------

TEST_CASE("PostProcessSystem: init does not crash in headless mode", "[postprocess]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::PostProcessSystem pps;
    REQUIRE(pps.init(1280, 720));

    // In headless (Noop) mode programs cannot be created from embedded shaders.
    // The system must initialise successfully and degrade gracefully.
    REQUIRE(pps.resources().width() == 1280);
    REQUIRE(pps.resources().height() == 720);

    pps.shutdown();
}

// ---------------------------------------------------------------------------
// PostProcessSystem::submit() — view count with bloom enabled vs disabled
// ---------------------------------------------------------------------------

TEST_CASE("PostProcessSystem: bloom enabled uses more views than bloom disabled", "[postprocess]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::PostProcessSystem pps;
    pps.init(1280, 720);

    engine::rendering::ShaderUniforms uniforms{};
    uniforms.init();

    // --- With bloom + FXAA enabled ---
    engine::rendering::PostProcessSettings settingsOn;
    settingsOn.bloom.enabled = true;
    settingsOn.bloom.downsampleSteps = 5;
    settingsOn.fxaaEnabled = true;

    bgfxCtx.renderer.beginFrame();
    const engine::rendering::ViewId viewIdAfterOn =
        pps.submit(settingsOn, uniforms, engine::rendering::kViewPostProcessBase);
    bgfxCtx.renderer.endFrame();

    const uint32_t viewsWithBloom =
        static_cast<uint32_t>(viewIdAfterOn - engine::rendering::kViewPostProcessBase);

    // --- With bloom disabled, FXAA enabled ---
    engine::rendering::PostProcessSettings settingsOff;
    settingsOff.bloom.enabled = false;
    settingsOff.fxaaEnabled = true;

    bgfxCtx.renderer.beginFrame();
    const engine::rendering::ViewId viewIdAfterOff =
        pps.submit(settingsOff, uniforms, engine::rendering::kViewPostProcessBase);
    bgfxCtx.renderer.endFrame();

    const uint32_t viewsWithoutBloom =
        static_cast<uint32_t>(viewIdAfterOff - engine::rendering::kViewPostProcessBase);

    // Bloom adds: 1 threshold + (steps-1) downsample + (steps-1) upsample = 1+4+4=9 passes
    // for steps=5.  The version without bloom should use fewer view IDs.
    REQUIRE(viewsWithBloom > viewsWithoutBloom);

    uniforms.destroy();
    pps.shutdown();
}

// ---------------------------------------------------------------------------
// PostProcessSystem::submit() — FXAA disabled skips the FXAA pass
// ---------------------------------------------------------------------------

TEST_CASE("PostProcessSystem: fxaaEnabled=false skips the FXAA pass", "[postprocess]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::PostProcessSystem pps;
    pps.init(1280, 720);

    engine::rendering::ShaderUniforms uniforms{};
    uniforms.init();

    engine::rendering::PostProcessSettings settingsFxaaOn;
    settingsFxaaOn.bloom.enabled = false;
    settingsFxaaOn.fxaaEnabled = true;

    bgfxCtx.renderer.beginFrame();
    const engine::rendering::ViewId afterFxaaOn =
        pps.submit(settingsFxaaOn, uniforms, engine::rendering::kViewPostProcessBase);
    bgfxCtx.renderer.endFrame();

    engine::rendering::PostProcessSettings settingsFxaaOff;
    settingsFxaaOff.bloom.enabled = false;
    settingsFxaaOff.fxaaEnabled = false;

    bgfxCtx.renderer.beginFrame();
    const engine::rendering::ViewId afterFxaaOff =
        pps.submit(settingsFxaaOff, uniforms, engine::rendering::kViewPostProcessBase);
    bgfxCtx.renderer.endFrame();

    // FXAA-on should consume exactly one more view than FXAA-off.
    REQUIRE(afterFxaaOn == afterFxaaOff + 1);

    uniforms.destroy();
    pps.shutdown();
}
