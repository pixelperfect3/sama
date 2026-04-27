// Android Test — cross-platform input test game.
//
// Renders a colored background that responds to input:
//   - Touch/click: changes hue based on touch X position
//   - Drag: leaves a trail of colored dots showing touch path
//   - Gyro tilt: shifts the background brightness based on device tilt
//   - Multi-touch: each finger shows a separate colored dot
//   - Keyboard: Space resets, Escape quits
//
// Also displays debug text showing input state (touch count, gyro values,
// mouse position, active keys).
//
// Desktop:  build/android_test  (mouse + keyboard simulate touch/gyro)
// Android:  linked into libsama_android.so via samaCreateGame()

#include <algorithm>
#include <cmath>
#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/TextureLoader.h"
#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/GameRunner.h"
#include "engine/game/IGame.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "engine/ui/BitmapFont.h"
#include "engine/ui/MsdfFont.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiRenderer.h"
#ifdef __ANDROID__
#include <android/asset_manager.h>

#include "engine/platform/android/AndroidFileSystem.h"
#include "engine/platform/android/AndroidGlobals.h"
#endif

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::game;
using namespace engine::input;
using namespace engine::rendering;

namespace
{

// Convert HSV (h in [0,360), s/v in [0,1]) to a packed RGBA uint32_t.
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

class AndroidTestGame : public IGame
{
public:
    void onInit(Engine& engine, Registry& registry) override
    {
        elapsed_ = 0.0f;
        hue_ = 200.0f;
        brightness_ = 0.4f;

        uiRenderer_.init();
        font_.createDebugFont();
        loadMsdfFont();

        // ----------------------------------------------------------------
        // Asset system: thread pool + platform filesystem + AssetManager.
        // Android reads from APK assets via AAssetManager; desktop reads
        // from the working directory.
        // ----------------------------------------------------------------
        threadPool_ = std::make_unique<engine::threading::ThreadPool>(2);
#ifdef __ANDROID__
        fileSystem_ = std::make_unique<engine::platform::AndroidFileSystem>(
            engine::platform::getAssetManager());
#else
        fileSystem_ = std::make_unique<engine::assets::StdFileSystem>("assets");
#endif
        assetManager_ = std::make_unique<engine::assets::AssetManager>(*threadPool_, *fileSystem_);
        assetManager_->registerLoader(std::make_unique<engine::assets::TextureLoader>());
        assetManager_->registerLoader(std::make_unique<engine::assets::GltfLoader>());

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
        groundMat.albedo = {0.85f, 0.82f, 0.78f, 1.0f};  // light beige for shadow contrast
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
        // On Android: real touch events. On desktop: mouse emulates first touch.
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
#ifdef __ANDROID__
        if (frameCount_ % 60 == 1)
        {
            uint32_t c = hsvToRgba(hue_, 0.6f, brightness_);
            __android_log_print(4, "SamaEngine",
                                "frame=%d color=R%u,G%u,B%u gyro: avail=%d "
                                "pitch=%.2f yaw=%.2f roll=%.2f "
                                "grav=(%.2f,%.2f,%.2f) hue=%.0f bright=%.2f touches=%zu",
                                frameCount_, (c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF,
                                gyro.available ? 1 : 0, gyro.pitchRate, gyro.yawRate, gyro.rollRate,
                                gyro.gravityX, gyro.gravityY, gyro.gravityZ, hue_, brightness_,
                                input.touches().size());
        }
#endif
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
        if (input.isKeyPressed(Key::Space))
        {
            // Reset
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
        // Use view 0 for maximum compatibility (works even without full
        // renderer setup on Android where shaders are stubbed).
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

        // Orbiting directional light. Elevation 0.55 keeps the light fairly
        // high so it doesn't dip below the horizon. NOTE: must NOT be
        // (0, 1, 0) — lookAt(lightPos, origin, up=(0,1,0)) is degenerate
        // when the look direction is parallel to up, producing NaN matrices.
        const float lightAngle = elapsed_ * 0.45f;
        const float kLightElevation = 0.55f;
        const float cosE = std::sqrt(1.0f - kLightElevation * kLightElevation);
        const glm::vec3 kLightDir = glm::normalize(
            glm::vec3(cosE * std::sin(lightAngle), kLightElevation, cosE * std::cos(lightAngle)));
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
        drawCallSys_.submitShadowDrawCalls(registry, engine.resources(), engine.shadowProgram(), 0);

        // Opaque PBR pass.
        const auto W = engine.fbWidth();
        const auto H = engine.fbHeight();
        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(bgColor)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = engine.shadow().shadowMatrix(0);
        const auto shadowAtlas = engine.shadow().atlasTexture();
#ifdef __ANDROID__
        if (frameCount_ == 60)  // log once after first second
        {
            // bgfx view stats — list each view + GPU time (proxy for "had draws")
            const bgfx::Stats* stats = bgfx::getStats();
            if (stats && stats->viewStats)
            {
                for (uint16_t i = 0; i < stats->numViews && i < 12; ++i)
                {
                    const bgfx::ViewStats& vs = stats->viewStats[i];
                    __android_log_print(4, "SamaEngine", "view %u name=%s gpu=%lld cpu=%lld",
                                        vs.view, vs.name,
                                        (long long)(vs.gpuTimeEnd - vs.gpuTimeBegin),
                                        (long long)(vs.cpuTimeEnd - vs.cpuTimeBegin));
                }
                __android_log_print(4, "SamaEngine", "stats: numDraw=%u numViews=%u numCompute=%u",
                                    stats->numDraw, stats->numViews, stats->numCompute);
            }
            __android_log_print(4, "SamaEngine", "shadow atlas valid=%d idx=%u",
                                bgfx::isValid(shadowAtlas) ? 1 : 0, shadowAtlas.idx);
            __android_log_print(4, "SamaEngine", "shadow matrix row0: %.3f %.3f %.3f %.3f",
                                shadowMat[0][0], shadowMat[0][1], shadowMat[0][2], shadowMat[0][3]);
            __android_log_print(4, "SamaEngine", "shadow matrix row3: %.3f %.3f %.3f %.3f",
                                shadowMat[3][0], shadowMat[3][1], shadowMat[3][2], shadowMat[3][3]);
            // Probe the shadow projection of the helmet position (0, 0.8, 0)
            glm::vec4 sc = shadowMat * glm::vec4(0.f, 0.8f, 0.f, 1.f);
            __android_log_print(4, "SamaEngine",
                                "shadow probe helmet (0,0.8,0) -> (%.3f, %.3f, %.3f, %.3f)", sc.x,
                                sc.y, sc.z, sc.w);
            // Probe the ground position right under the helmet
            glm::vec4 sg = shadowMat * glm::vec4(0.f, -0.55f, 0.f, 1.f);
            __android_log_print(4, "SamaEngine",
                                "shadow probe ground (0,-0.55,0) -> (%.3f, %.3f, %.3f, %.3f)", sg.x,
                                sg.y, sg.z, sg.w);
            // Print the actual world position + mesh stats of every
            // shadow caster, to confirm the helmet has valid geometry.
            registry.view<ShadowVisibleTag, WorldTransformComponent, MeshComponent>().each(
                [&](EntityID e, const ShadowVisibleTag&, const WorldTransformComponent& wtc,
                    const MeshComponent& mc)
                {
                    const auto* mesh = engine.resources().getMesh(mc.mesh);
                    bool meshValid = mesh && mesh->isValid();
                    bool posValid = mesh && bgfx::isValid(mesh->positionVbh);
                    bool ibValid = mesh && bgfx::isValid(mesh->ibh);
                    __android_log_print(4, "SamaEngine",
                                        "caster e=%u meshId=%u verts=%u idx=%u "
                                        "world=(%.2f,%.2f,%.2f) "
                                        "meshValid=%d posVbh=%d ibh=%d",
                                        static_cast<uint32_t>(e), mc.mesh,
                                        mesh ? mesh->vertexCount : 0, mesh ? mesh->indexCount : 0,
                                        wtc.matrix[3][0], wtc.matrix[3][1], wtc.matrix[3][2],
                                        meshValid ? 1 : 0, posValid ? 1 : 0, ibValid ? 1 : 0);
                });
        }
#endif
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
        drawCallSys_.update(registry, engine.resources(), engine.pbrProgram(), engine.uniforms(),
                            frame);

        // --- Text overlay via UiRenderer ---
        {
            using engine::ui::UiDrawList;
            drawList_.clear();

            char buf[256];
#ifdef __ANDROID__
            // Larger margin for rounded screens + status bar (Pixel 9)
            float y = 160.f;
            const float leftMargin = 40.f;
            const float fontSize = 14.f;
            const float lineH = 18.f;
#else
            float y = 10.f;
            const float leftMargin = 10.f;
            const float fontSize = 16.f;
            const float lineH = 20.f;
#endif
            const glm::vec4 white{1.f, 1.f, 1.f, 1.f};
            const glm::vec4 green{0.4f, 1.f, 0.4f, 1.f};
            const glm::vec4 gray{0.7f, 0.7f, 0.7f, 1.f};

            snprintf(buf, sizeof(buf), "Android Test | %.1f fps | %.3f ms",
                     dt > 0 ? 1.0f / dt : 0.0f, dt * 1000.0f);
            drawList_.drawText({leftMargin, y}, buf, white, &font_, fontSize);
            y += lineH;

            snprintf(buf, sizeof(buf), "Screen: %ux%u", engine.fbWidth(), engine.fbHeight());
            drawList_.drawText({leftMargin, y}, buf, white, &font_, fontSize);
            y += lineH * 1.5f;

            // Mouse / touch
            snprintf(buf, sizeof(buf), "Mouse: (%.0f, %.0f)  %s",
                     static_cast<double>(input.mouseX()), static_cast<double>(input.mouseY()),
                     input.isMouseButtonHeld(MouseButton::Left) ? "[LEFT]" : "");
            drawList_.drawText({leftMargin, y}, buf, gray, &font_, fontSize);
            y += lineH;

            snprintf(buf, sizeof(buf), "Touches: %zu active", input.touches().size());
            drawList_.drawText({leftMargin, y}, buf, gray, &font_, fontSize);
            y += lineH;

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
                snprintf(buf, sizeof(buf), "  [%llu] (%.0f, %.0f) %s",
                         static_cast<unsigned long long>(touch.id), touch.x, touch.y, phase);
                drawList_.drawText({leftMargin + 10.f, y}, buf, gray, &font_, fontSize);
                y += lineH;
                if (++touchRow >= 5)
                    break;
            }
            y += lineH * 0.5f;

            // Gyro
            if (gyro.available)
            {
                snprintf(buf, sizeof(buf), "Gyro: pitch=%.2f  yaw=%.2f  roll=%.2f", gyro.pitchRate,
                         gyro.yawRate, gyro.rollRate);
                drawList_.drawText({leftMargin, y}, buf, green, &font_, fontSize);
                y += lineH;
                snprintf(buf, sizeof(buf), "Gravity: (%.2f, %.2f, %.2f)", gyro.gravityX,
                         gyro.gravityY, gyro.gravityZ);
                drawList_.drawText({leftMargin, y}, buf, green, &font_, fontSize);
                y += lineH;
            }
            else
            {
                drawList_.drawText({leftMargin, y}, "Gyro: not available", gray, &font_, fontSize);
                y += lineH;
            }
            y += lineH * 0.5f;

            // Trail + color info
            snprintf(buf, sizeof(buf), "Trail dots: %zu", touchTrail_.size());
            drawList_.drawText({leftMargin, y}, buf, gray, &font_, fontSize);
            y += lineH;
            snprintf(buf, sizeof(buf), "Hue: %.0f  Brightness: %.2f", hue_, brightness_);
            drawList_.drawText({leftMargin, y}, buf, gray, &font_, fontSize);
            y += lineH * 1.5f;

            // Controls
            drawList_.drawText({leftMargin, y}, "--- Controls ---", gray, &font_, fontSize);
            y += lineH;
            drawList_.drawText({leftMargin, y}, "Touch/Click: change hue by X", gray, &font_,
                               fontSize);
            y += lineH;
            drawList_.drawText({leftMargin, y}, "Drag: draw colored trail", gray, &font_, fontSize);
            y += lineH;
            drawList_.drawText({leftMargin, y}, "Gyro tilt: adjust brightness + hue", gray, &font_,
                               fontSize);
            y += lineH;
            drawList_.drawText({leftMargin, y}, "Space: reset | Escape: quit", gray, &font_,
                               fontSize);
            y += lineH;

            snprintf(buf, sizeof(buf), "Color: R=%u G=%u B=%u  (hue=%.0f sat=0.6 val=%.2f)",
                     (bgColor >> 24) & 0xFF, (bgColor >> 16) & 0xFF, (bgColor >> 8) & 0xFF, hue_,
                     brightness_);
            drawList_.drawText({leftMargin, y}, buf, gray, &font_, fontSize);
            y += lineH;

            // Helmet asset status
            const char* helmetStatus = "Loading…";
            glm::vec4 helmetColor = gray;
            if (helmetSpawned_)
            {
                helmetStatus = "Ready (PBR + shadow)";
                helmetColor = green;
            }
            else if (assetManager_ &&
                     assetManager_->state(helmetHandle_) == engine::assets::AssetState::Failed)
            {
                helmetStatus = "FAILED to load";
                helmetColor = glm::vec4{1.f, 0.4f, 0.4f, 1.f};
            }
            snprintf(buf, sizeof(buf), "DamagedHelmet.glb: %s", helmetStatus);
            drawList_.drawText({leftMargin, y}, buf, helmetColor, &font_, fontSize);
            y += lineH * 1.5f;

            // MSDF font test line
            if (msdfFont_.glyphCount() > 0)
            {
                const glm::vec4 yellow{1.f, 0.95f, 0.4f, 1.f};
                drawList_.drawText({leftMargin, y}, "ChunkFive MSDF: The quick brown fox!", yellow,
                                   &msdfFont_, fontSize * 1.5f);
                y += lineH * 2.f;
            }

            // Shadow atlas debug viewer — bottom-left corner. Shows what the
            // light "sees" — should contain the helmet's silhouette in white
            // (close to light) on a black background (cleared depth = far).
            const auto shadowAtlas = engine.shadow().atlasTexture();
            if (bgfx::isValid(shadowAtlas))
            {
                const float panel = 240.f;
                drawList_.drawRect({18.f, fbH - panel - 18.f}, {panel + 4.f, panel + 4.f},
                                   {0.0f, 0.0f, 0.0f, 1.0f}, 0.f);
                drawList_.drawTexturedRect({20.f, fbH - panel - 16.f}, {panel, panel}, shadowAtlas,
                                           {0.f, 0.f, 1.f, 1.f}, {1.f, 1.f, 1.f, 1.f});
                drawList_.drawText({22.f, fbH - panel - 36.f}, "shadow atlas (depth)", white,
                                   &font_, 12.f);
            }

            // Render UI on view 48 (kViewGameUi)
            uiRenderer_.render(drawList_, 48, engine.fbWidth(), engine.fbHeight());
        }
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

    // UI text rendering (works on both desktop and Android)
    engine::ui::UiRenderer uiRenderer_;
    engine::ui::BitmapFont font_;
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

    void loadMsdfFont()
    {
#ifdef __ANDROID__
        AAssetManager* am = engine::platform::getAssetManager();
        if (!am)
            return;
        auto readAsset = [&](const char* path) -> std::vector<uint8_t>
        {
            AAsset* asset = AAssetManager_open(am, path, AASSET_MODE_BUFFER);
            if (!asset)
                return {};
            auto len = AAsset_getLength(asset);
            const void* buf = AAsset_getBuffer(asset);
            std::vector<uint8_t> data(static_cast<size_t>(len));
            std::memcpy(data.data(), buf, data.size());
            AAsset_close(asset);
            return data;
        };
        auto json = readAsset("fonts/ChunkFive-msdf.json");
        auto png = readAsset("fonts/ChunkFive-msdf.png");
        if (!json.empty() && !png.empty())
            msdfFont_.loadFromMemory(json.data(), json.size(), png.data(), png.size());
#else
        msdfFont_.loadFromFile("assets/fonts/ChunkFive-msdf.json",
                               "assets/fonts/ChunkFive-msdf.png");
#endif
    }
};

// ---------------------------------------------------------------------------
// Entry points — desktop and Android
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

engine::game::IGame* samaCreateGame()
{
    return new AndroidTestGame();
}

#else

int main()
{
    AndroidTestGame game;
    GameRunner runner(game);

    EngineDesc desc;
    desc.windowTitle = "Android Test — Input";
    desc.windowWidth = 800;
    desc.windowHeight = 600;
    return runner.run(desc);
}

#endif
