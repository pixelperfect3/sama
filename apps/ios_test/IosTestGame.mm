// iOS Test — port of apps/android_test/AndroidTestGame.cpp.
//
// Demonstrates the same gameplay/visualization features as the Android test
// app on iOS:
//
//   - Touch (drag): leaves a colored hue trail; X position drives base hue.
//   - Multi-touch: each finger contributes a dot.
//   - Gyro: tilt forward/back adjusts brightness; yaw shifts hue.
//   - DamagedHelmet glTF scene with ground plane and orbiting light, lit by
//     a directional light + shadow cascade pass (same as Android).
//   - DebugHud overlay: FPS, frame time, touch count, gyro pitch/yaw/roll,
//     gravity vector, helmet load status, controls.
//
// Objective-C++ (.mm) is required only because the file lives next to the
// iOS application bootstrap and is compiled by the iOS toolchain. The bulk
// of the game logic is plain C++ that mirrors AndroidTestGame line-for-line;
// the only iOS-specific code is constructing IosFileSystem in onInit().
//
// Build wiring (CMakeLists.txt) and Engine::initIos(...) integration are
// handled by other agents — this file uses only public engine APIs that
// already exist for Android.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <vector>

#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/TextureLoader.h"
#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/IGame.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"
#include "engine/platform/ios/IosApp.h"
#include "engine/platform/ios/IosFileSystem.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "engine/ui/DebugHud.h"
#include "engine/ui/MsdfFont.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiRenderer.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::game;
using namespace engine::input;
using namespace engine::rendering;

namespace
{

// Convert HSV (h in [0,360), s/v in [0,1]) to a packed RGBA uint32_t.
// Identical to AndroidTestGame so the colour cycling matches exactly.
uint32_t hsvToRgba(float h, float s, float v)
{
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r = 0, g = 0, b = 0;
    if (h < 60)
    {
        r = c;
        g = x;
    }
    else if (h < 120)
    {
        r = x;
        g = c;
    }
    else if (h < 180)
    {
        g = c;
        b = x;
    }
    else if (h < 240)
    {
        g = x;
        b = c;
    }
    else if (h < 300)
    {
        r = x;
        b = c;
    }
    else
    {
        r = c;
        b = x;
    }

    auto toU8 = [](float f) -> uint8_t
    { return static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };

    return (toU8(r + m) << 24) | (toU8(g + m) << 16) | (toU8(b + m) << 8) | 0xFF;
}

}  // namespace

class IosTestGame : public IGame
{
public:
    void onInit(Engine& engine, Registry& registry) override
    {
        elapsed_ = 0.0f;
        hue_ = 200.0f;
        brightness_ = 0.4f;

        hud_.init();

        // ----------------------------------------------------------------
        // Asset system: thread pool + IosFileSystem (NSBundle-backed) +
        // AssetManager. iOS reads from the application bundle, the same
        // way Android reads from the APK's assets/ directory.
        // ----------------------------------------------------------------
        threadPool_ = std::make_unique<engine::threading::ThreadPool>(2);
        fileSystem_ = std::make_unique<engine::platform::ios::IosFileSystem>();
        assetManager_ = std::make_unique<engine::assets::AssetManager>(*threadPool_, *fileSystem_);
        assetManager_->registerLoader(std::make_unique<engine::assets::TextureLoader>());
        assetManager_->registerLoader(std::make_unique<engine::assets::GltfLoader>());

        // MSDF text — load ChunkFive from the app bundle (bundled by
        // SamaIosAssets.cmake). Renders on its own UI view above the HUD.
        // fileSystem_ is constructed above; this must run after it.
        uiRenderer_.init();
        if (fileSystem_)
        {
            const auto json = fileSystem_->read("fonts/ChunkFive-msdf.json");
            const auto png = fileSystem_->read("fonts/ChunkFive-msdf.png");
            std::fprintf(stderr, "[ios_test] MSDF font bytes: json=%zu png=%zu\n", json.size(),
                         png.size());
            if (!json.empty() && !png.empty())
            {
                const bool ok =
                    msdfFont_.loadFromMemory(json.data(), json.size(), png.data(), png.size());
                std::fprintf(stderr, "[ios_test] msdfFont_.loadFromMemory: %s, glyphs=%zu\n",
                             ok ? "ok" : "FAILED", msdfFont_.glyphCount());
            }
        }

        // Procedural sky/ground IBL — gives the helmet realistic env reflections.
        ibl_.generateDefault();

        // Kick off async helmet load (will spawn on first frame it's Ready).
        helmetHandle_ = assetManager_->load<engine::assets::GltfAsset>("DamagedHelmet.glb");

        // ----------------------------------------------------------------
        // Ground plane (a thin scaled cube) to receive the helmet's shadow.
        // ----------------------------------------------------------------
        MeshData cubeData = makeCubeMeshData();
        Mesh cubeMesh = buildMesh(cubeData);
        groundMeshId_ = engine.resources().addMesh(std::move(cubeMesh));

        Material groundMat;
        groundMat.albedo = {1.0f, 1.0f, 1.0f, 1.0f};  // pure white — maximum shadow contrast
        groundMat.roughness = 0.9f;
        groundMat.metallic = 0.0f;
        groundMatId_ = engine.resources().addMaterial(groundMat);

        groundEntity_ = registry.createEntity();
        TransformComponent gtc;
        gtc.position = {0.0f, -0.55f, 0.0f};
        gtc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        gtc.scale = {8.0f, 0.05f, 8.0f};
        gtc.flags = 1;
        registry.emplace<TransformComponent>(groundEntity_, gtc);
        registry.emplace<WorldTransformComponent>(groundEntity_);
        registry.emplace<MeshComponent>(groundEntity_, groundMeshId_);
        registry.emplace<MaterialComponent>(groundEntity_, groundMatId_);
        registry.emplace<VisibleTag>(groundEntity_);
        // NOTE: deliberately no ShadowVisibleTag on the ground — it should
        // only RECEIVE shadows (via the PBR shader sampling the atlas).

        // ----------------------------------------------------------------
        // Light indicator — small emissive cube placed at the light
        // position each frame so you can see where the light is.
        // ----------------------------------------------------------------
        Material lightMat;
        lightMat.albedo = {1.0f, 0.9f, 0.3f, 1.0f};
        lightMat.emissiveScale = 5.0f;
        lightMat.roughness = 1.0f;
        lightMatId_ = engine.resources().addMaterial(lightMat);

        lightEntity_ = registry.createEntity();
        TransformComponent ltc;
        ltc.position = {0.0f, 3.0f, 0.0f};
        ltc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        ltc.scale = {0.4f, 0.4f, 0.4f};
        ltc.flags = 1;
        registry.emplace<TransformComponent>(lightEntity_, ltc);
        registry.emplace<WorldTransformComponent>(lightEntity_);
        registry.emplace<MeshComponent>(lightEntity_, groundMeshId_);  // reuse cube mesh
        registry.emplace<MaterialComponent>(lightEntity_, lightMatId_);
        registry.emplace<VisibleTag>(lightEntity_);
        // Light cube does NOT cast shadow on itself / scene.
    }

    void onUpdate(Engine& engine, Registry& registry, float dt) override
    {
        elapsed_ += dt;
        frameCount_++;
        const auto& input = engine.inputState();
        const float fbW = static_cast<float>(engine.fbWidth());
        const float fbH = static_cast<float>(engine.fbHeight());

        // --- Touch / mouse input ---
        // On iOS: real touch events. The first touch is also mapped to the
        // mouse cursor by IosTouchInput, so the desktop-style mouse path
        // works identically to AndroidTestGame.
        if (input.isMouseButtonHeld(MouseButton::Left))
        {
            // Map mouse X to hue (0..360)
            float normX = static_cast<float>(input.mouseX()) / std::max(fbW, 1.0f);
            hue_ = normX * 360.0f;

            // Track touch trail
            TouchDot dot;
            dot.x = static_cast<float>(input.mouseX());
            dot.y = static_cast<float>(input.mouseY());
            dot.hue = hue_;
            dot.age = 0.0f;
            touchTrail_.push_back(dot);
        }

        // Multi-touch: each touch gets a dot
        for (const auto& touch : input.touches())
        {
            if (touch.phase == TouchPoint::Phase::Began || touch.phase == TouchPoint::Phase::Moved)
            {
                TouchDot dot;
                dot.x = touch.x;
                dot.y = touch.y;
                dot.hue = std::fmod(static_cast<float>(touch.id) * 60.0f, 360.0f);
                dot.age = 0.0f;
                touchTrail_.push_back(dot);
            }
        }

        // Age and prune trail dots (fade over 2 seconds)
        for (auto& dot : touchTrail_)
            dot.age += dt;
        touchTrail_.erase(std::remove_if(touchTrail_.begin(), touchTrail_.end(),
                                         [](const TouchDot& d) { return d.age > 2.0f; }),
                          touchTrail_.end());

        // --- Gyro input ---
        const auto& gyro = input.gyro();
        if (gyro.available)
        {
            // Tilt forward/back adjusts brightness
            brightness_ = std::clamp(0.4f + gyro.gravityZ * 0.3f, 0.1f, 0.8f);

            // Tilt left/right shifts hue
            hue_ += gyro.yawRate * dt * 60.0f;
            if (hue_ < 0.0f)
                hue_ += 360.0f;
            if (hue_ >= 360.0f)
                hue_ -= 360.0f;
        }

        // --- Keyboard ---
        // iOS: deferred — there is no hardware-keyboard test on iOS Phone in
        // this sample. Space/Escape would require a soft keyboard overlay
        // which is out of scope for the test app.
        if (input.isKeyPressed(Key::Space))
        {
            // Reset (still useful when a Bluetooth keyboard is attached or
            // when the same code path runs on a desktop debug build).
            hue_ = 200.0f;
            brightness_ = 0.4f;
            touchTrail_.clear();
        }

        // Slow auto-cycle when idle (no touch)
        if (!input.isMouseButtonHeld(MouseButton::Left) && input.touches().empty())
        {
            hue_ = std::fmod(hue_ + dt * 10.0f, 360.0f);
        }

        // --- Render ---
        uint32_t bgColor = hsvToRgba(hue_, 0.6f, brightness_);
        engine.setClearColor(bgColor);

        // --- Asset uploads + spawn helmet on Ready ------------------------
        assetManager_->processUploads();
        const engine::assets::AssetState helmetState = assetManager_->state(helmetHandle_);
        if (!helmetSpawned_ && helmetState == engine::assets::AssetState::Ready)
        {
            const auto* helmet = assetManager_->get<engine::assets::GltfAsset>(helmetHandle_);
            if (helmet)
            {
                engine::assets::GltfSceneSpawner::spawn(*helmet, registry, engine.resources());
                helmetSpawned_ = true;

                // Float the spawned helmet above the ground (keep its
                // original ~1m size so it casts a substantial shadow).
                // GltfSceneSpawner already adds ShadowVisibleTag to spawned
                // mesh entities, so we don't need to do that ourselves.
                // IMPORTANT: skip the ground and the light indicator —
                // we manage their positions ourselves and they must NOT
                // be touched (especially: light cube must never become a
                // shadow caster).
                registry.view<TransformComponent, MeshComponent>().each(
                    [&](EntityID e, TransformComponent& tc, const MeshComponent&)
                    {
                        if (e == groundEntity_ || e == lightEntity_)
                            return;
                        tc.position.y += 0.8f;
                        tc.flags |= 1;
                    });
                // Defensive: never let the light cube or the ground become
                // a shadow caster.
                if (registry.has<ShadowVisibleTag>(lightEntity_))
                    registry.remove<ShadowVisibleTag>(lightEntity_);
                if (registry.has<ShadowVisibleTag>(groundEntity_))
                    registry.remove<ShadowVisibleTag>(groundEntity_);

                // Tweak roughness slightly so the helmet looks less mirror-like.
                registry.view<MaterialComponent>().each(
                    [&](EntityID, MaterialComponent& mc)
                    {
                        auto* mat = engine.resources().getMaterialMut(mc.material);
                        if (mat && mc.material != groundMatId_)
                            mat->roughness = 0.55f;
                    });
            }
        }

        transformSys_.update(registry);

        // Camera pulled way back so the entire scene — helmet, ground,
        // light indicator cube, and the helmet's cast shadow — all fit
        // comfortably in frame.
        const float aspect = (fbH > 0.f) ? (fbW / fbH) : 1.0f;
        const glm::vec3 camPos{0.0f, 5.0f, 13.0f};
        const glm::vec3 camTarget{0.0f, 0.0f, 0.0f};
        const glm::mat4 viewMat = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));
        const glm::mat4 projMat = glm::perspective(glm::radians(45.f), aspect, 0.05f, 50.f);

        // Fixed directional light from upper-front-right.  Locked (not
        // orbiting) so the cast shadow on the ground is in a predictable
        // place — easier to spot during development.  Avoids the
        // (0, 1, 0) degeneracy of lookAt(origin, origin, up=(0,1,0)).
        const glm::vec3 kLightDir = glm::normalize(glm::vec3(0.6f, 1.2f, 0.8f));
        constexpr float kLightIntens = 18.0f;

        // Move the light indicator cube to follow the directional light.
        // 5m away from the helmet keeps it clearly outside the model.
        if (auto* ltc = registry.get<TransformComponent>(lightEntity_))
        {
            ltc->position = kLightDir * 5.0f;
            ltc->flags |= 1;
        }
        const float lightData[8] = {
            kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
            1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};
        const glm::vec3 lightPos = kLightDir * 10.f;
        const glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
        // Ortho frustum sized to cover the entire 8x8 ground (with margin
        // for light-angle skew) so every ground fragment sees the shadow
        // check. With a smaller frustum, the shader's [0,1] range check
        // creates a visible boundary on the ground that looks like a
        // cube-shaped shadow.
        const glm::mat4 lightProj = glm::ortho(-6.f, 6.f, -6.f, 6.f, 0.1f, 30.f);

        // Shadow pass — depth-only into cascade 0.
        engine.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys_.submitShadowDrawCalls(registry, engine.resources(),
                                           bgfx::ProgramHandle{engine.shadowProgram().idx}, 0);

        // One-shot shadow diagnostic at frame 100.
        if (frameCount_ == 100)
        {
            int casters = 0;
            registry.view<ShadowVisibleTag, MeshComponent>().each(
                [&](EntityID, const ShadowVisibleTag&, const MeshComponent&) { ++casters; });
            const auto atlas = engine.shadow().atlasTexture();
            const glm::mat4 sm = engine.shadow().shadowMatrix(0);
            std::fprintf(stderr,
                         "[ios_test shadow] casters=%d atlasValid=%d "
                         "shadowMat[0]=(%.2f,%.2f,%.2f,%.2f) lightDir=(%.2f,%.2f,%.2f) "
                         "shadowProgValid=%d\n",
                         casters, bgfx::isValid(atlas), sm[0][0], sm[0][1], sm[0][2], sm[0][3],
                         kLightDir.x, kLightDir.y, kLightDir.z,
                         engine::rendering::isValid(engine.shadowProgram()));
        }

        // Opaque PBR pass.
        const auto W = engine.fbWidth();
        const auto H = engine.fbHeight();
        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(bgColor)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = engine.shadow().shadowMatrix(0);
        const auto shadowAtlas = engine.shadow().atlasTexture();
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), shadowAtlas, W, H, 0.05f, 50.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;
        // IBL disabled to make the helmet's cast shadow visible against the
        // ground. With IBL fill, the shadowed area still receives strong
        // ambient illumination from the environment and the shadow gets
        // washed out almost entirely.
        // if (ibl_.isValid()) { ... }
        drawCallSys_.update(registry, engine.resources(),
                            bgfx::ProgramHandle{engine.pbrProgram().idx}, engine.uniforms(), frame);

        // --- DebugHud overlay ------------------------------------------------
        // Same content as AndroidTestGame's hand-rolled UiDrawList overlay,
        // but rendered through the platform-agnostic DebugHud helper. Cells
        // are 8x16 px columns/rows; we keep a small left-margin column so
        // text doesn't run into rounded screen corners on iPhone.
        hud_.begin(static_cast<uint32_t>(W), static_cast<uint32_t>(H));

        const uint32_t kWhite = 0xFFFFFFFFu;
        const uint32_t kGreen = 0x66FF66FFu;
        const uint32_t kGray = 0xB0B0B0FFu;
        const uint32_t kRed = 0xFF6666FFu;

        uint16_t row = 1;
        hud_.printf(2, row++, kWhite, "iOS Test  |  %.1f fps  |  %.3f ms",
                    dt > 0 ? 1.0f / dt : 0.0f, dt * 1000.0f);
        hud_.printf(2, row++, kWhite, "Screen: %ux%u", engine.fbWidth(), engine.fbHeight());
        row++;

        // Mouse / first-touch (IosTouchInput maps the first finger here)
        hud_.printf(2, row++, kGray, "Mouse: (%.0f, %.0f)  %s", static_cast<double>(input.mouseX()),
                    static_cast<double>(input.mouseY()),
                    input.isMouseButtonHeld(MouseButton::Left) ? "[DOWN]" : "");

        // Touch list
        hud_.printf(2, row++, kGray, "Touches: %zu active", input.touches().size());
        int touchRow = 0;
        for (const auto& touch : input.touches())
        {
            const char* phase = "?";
            if (touch.phase == TouchPoint::Phase::Began)
                phase = "Began";
            else if (touch.phase == TouchPoint::Phase::Moved)
                phase = "Moved";
            else if (touch.phase == TouchPoint::Phase::Ended)
                phase = "Ended";
            hud_.printf(4, row++, kGray, "[%llu] (%.0f, %.0f) %s",
                        static_cast<unsigned long long>(touch.id), touch.x, touch.y, phase);
            if (++touchRow >= 5)
                break;
        }
        row++;

        // Gyro
        if (gyro.available)
        {
            hud_.printf(2, row++, kGreen, "Gyro: pitch=%.2f  yaw=%.2f  roll=%.2f", gyro.pitchRate,
                        gyro.yawRate, gyro.rollRate);
            hud_.printf(2, row++, kGreen, "Gravity: (%.2f, %.2f, %.2f)", gyro.gravityX,
                        gyro.gravityY, gyro.gravityZ);
        }
        else
        {
            hud_.printf(2, row++, kGray, "Gyro: not available");
        }
        row++;

        // Trail + colour info
        hud_.printf(2, row++, kGray, "Trail dots: %zu", touchTrail_.size());
        hud_.printf(2, row++, kGray, "Hue: %.0f  Brightness: %.2f", hue_, brightness_);
        row++;

        // Controls
        hud_.printf(2, row++, kGray, "--- Controls ---");
        hud_.printf(2, row++, kGray, "Touch/Drag: change hue + leave trail");
        hud_.printf(2, row++, kGray, "Gyro tilt: adjust brightness + hue");
        // iOS: deferred — no hardware Space/Escape on touch-only devices.
        // Bluetooth keyboards still trigger isKeyPressed, so the line is
        // documented for parity with Android.
        hud_.printf(2, row++, kGray, "Space (BT keyboard): reset");

        hud_.printf(2, row++, kGray, "Color: R=%u G=%u B=%u  (hue=%.0f val=%.2f)",
                    (bgColor >> 24) & 0xFF, (bgColor >> 16) & 0xFF, (bgColor >> 8) & 0xFF, hue_,
                    brightness_);

        // Helmet asset status
        const char* helmetStatus = "Loading...";
        uint32_t helmetColor = kGray;
        if (helmetSpawned_)
        {
            helmetStatus = "Ready (PBR + shadow)";
            helmetColor = kGreen;
        }
        else if (assetManager_ &&
                 assetManager_->state(helmetHandle_) == engine::assets::AssetState::Failed)
        {
            helmetStatus = "FAILED to load";
            helmetColor = kRed;
        }
        hud_.printf(2, row++, helmetColor, "DamagedHelmet.glb: %s", helmetStatus);

        hud_.end();

        // --- MSDF text overlay -----------------------------------------------
        // Vector text on top of the bitmap HUD — mirrors AndroidTestGame's
        // "ChunkFive MSDF: The quick brown fox!" line.  Renders only if the
        // font loaded successfully in onInit (otherwise glyphCount == 0).
        drawList_.clear();
        if (msdfFont_.glyphCount() > 0)
        {
            const glm::vec4 yellow{1.0f, 0.95f, 0.4f, 1.0f};
            const float fontSize = 32.0f;
            const float xPos = 40.0f;
            const float yPos = static_cast<float>(fbH) - 80.0f;
            drawList_.drawText({xPos, yPos}, "ChunkFive MSDF: The quick brown fox!", yellow,
                               &msdfFont_, fontSize);
        }
        // Debug: draw the shadow atlas top-right corner so we can see if the
        // depth pass actually wrote anything into it.  If the atlas is empty
        // (uniform black or white), the shadow path is broken upstream.
        const auto debugAtlas = engine.shadow().atlasTexture();
        if (bgfx::isValid(debugAtlas))
        {
            const float panel = 360.0f;
            const float xRight = static_cast<float>(fbW) - panel - 30.0f;
            const float yTop = 30.0f;
            drawList_.drawRect({xRight - 4.0f, yTop - 4.0f}, {panel + 8.0f, panel + 8.0f},
                               {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f);
            drawList_.drawTexturedRect({xRight, yTop}, {panel, panel}, debugAtlas, {0, 0, 1, 1},
                                       {1, 1, 1, 1});
        }
        uiRenderer_.render(drawList_, kViewGameUi, static_cast<uint16_t>(fbW),
                           static_cast<uint16_t>(fbH));
    }

    void onShutdown(Engine& engine, Registry& registry) override
    {
        (void)engine;
        (void)registry;
        hud_.shutdown();
        // Asset manager + thread pool destroy automatically via unique_ptr.
    }

private:
    float elapsed_ = 0.0f;
    float hue_ = 200.0f;
    float brightness_ = 0.4f;
    int frameCount_ = 0;

    struct TouchDot
    {
        float x = 0.0f;
        float y = 0.0f;
        float hue = 0.0f;
        float age = 0.0f;
    };
    std::vector<TouchDot> touchTrail_;

    // Platform-agnostic debug text overlay (works on iOS via UiRenderer +
    // BitmapFont). Replaces Android's hand-rolled UiDrawList.
    engine::ui::DebugHud hud_;

    // Vector text overlay — MsdfFont rendered through UiRenderer on view
    // kViewGameUi.  Loaded from the app bundle in onInit and drawn each
    // frame after the HUD; falls through silently if the font is missing.
    engine::ui::UiRenderer uiRenderer_;
    engine::ui::MsdfFont msdfFont_;
    engine::ui::UiDrawList drawList_;

    // PBR scene — DamagedHelmet on a ground plane, lit by an orbiting light.
    engine::scene::TransformSystem transformSys_;
    DrawCallBuildSystem drawCallSys_;
    std::unique_ptr<engine::threading::ThreadPool> threadPool_;
    std::unique_ptr<engine::assets::IFileSystem> fileSystem_;
    std::unique_ptr<engine::assets::AssetManager> assetManager_;
    engine::rendering::IblResources ibl_;
    engine::assets::AssetHandle<engine::assets::GltfAsset> helmetHandle_{};
    bool helmetSpawned_ = false;
    EntityID groundEntity_ = INVALID_ENTITY;
    EntityID lightEntity_ = INVALID_ENTITY;
    uint32_t groundMeshId_ = 0;
    uint32_t groundMatId_ = 0;
    uint32_t lightMatId_ = 0;
};

// ---------------------------------------------------------------------------
// Game entry point — mirrors the Android `samaCreateGame()` contract. The
// iOS application bootstrap (engine/platform/ios/IosApp.mm) calls this once
// at launch and takes ownership of the returned IGame.
// ---------------------------------------------------------------------------

engine::game::IGame* samaCreateGame()
{
    return new IosTestGame();
}
