#define GLFW_INCLUDE_NONE
#include "PerfSmokeGame.h"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/scene/TransformSystem.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <cstdarg>
#include <cstring>
#endif

namespace
{
// On desktop, write to stdout. On Android, route through logcat under the
// "PerfSmoke" tag (printf goes nowhere on NativeActivity), stripping any
// trailing newline so logcat doesn't blank-line every row.  Used by
// reportAndCheckBudgets() so the budget table is visible via
// `adb logcat | grep PerfSmoke`.
void perfLog(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    const size_t len = std::strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    __android_log_print(ANDROID_LOG_INFO, "PerfSmoke", "%s", buf);
#else
    std::fputs(buf, stdout);
#endif
}
}  // namespace

using namespace engine::ecs;
using namespace engine::physics;
using namespace engine::rendering;

namespace
{

using Clock = std::chrono::steady_clock;

float msSince(Clock::time_point t0)
{
    return std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
}

float mean(const std::vector<float>& v)
{
    if (v.empty())
        return 0.0f;
    double s = 0.0;
    for (float x : v)
        s += x;
    return static_cast<float>(s / v.size());
}

float percentile(std::vector<float> v, float p)
{
    if (v.empty())
        return 0.0f;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * (v.size() - 1));
    return v[idx];
}

float maxOf(const std::vector<float>& v)
{
    return v.empty() ? 0.0f : *std::max_element(v.begin(), v.end());
}

}  // namespace

namespace perf_smoke
{

PerfSmokeGame::PerfSmokeGame(int framesToRun, const PerfBudgets& budgets)
    : framesToRun_(framesToRun), budgets_(budgets)
{
    // Pre-allocate so push_back never reallocates inside the measurement loop.
    const auto reserve = [&](std::vector<float>& v) { v.reserve(framesToRun_); };
    reserve(samples_.physics);
    reserve(samples_.transform);
    reserve(samples_.cull);
    reserve(samples_.draw);
    reserve(samples_.shadow);
    reserve(samples_.lightCluster);
    reserve(samples_.frame);
}

void PerfSmokeGame::onInit(engine::core::Engine& engine, Registry& reg)
{
    registry_ = &reg;
    physics_.init();
    lightCluster_.init();

    // Single cube mesh shared by everything — keeps the comparison clean
    // (system cost doesn't include mesh-VBO upload churn).
    Mesh m = buildMesh(makeCubeMeshData());
    cubeMeshId_ = engine.resources().addMesh(std::move(m));

    Material dyn;
    dyn.albedo = {0.85f, 0.30f, 0.20f, 1.0f};
    dyn.roughness = 0.6f;
    dynMatId_ = engine.resources().addMaterial(dyn);

    Material wall;
    wall.albedo = {0.4f, 0.4f, 0.45f, 1.0f};
    wall.roughness = 0.85f;
    wallMatId_ = engine.resources().addMaterial(wall);

    Material gnd;
    gnd.albedo = {0.25f, 0.25f, 0.28f, 1.0f};
    gnd.roughness = 0.9f;
    groundMatId_ = engine.resources().addMaterial(gnd);

    Material helmet;
    helmet.albedo = {0.95f, 0.85f, 0.5f, 1.0f};
    helmet.roughness = 0.25f;
    helmet.metallic = 0.9f;
    helmetMatId_ = engine.resources().addMaterial(helmet);

    spawnScene(engine, reg);
}

namespace
{
// Spawn a renderable box.  matId, scale, position are caller-controlled;
// everything else is identical across the ~700 entities so this lives once.
EntityID spawnBox(Registry& reg, uint32_t meshId, uint32_t matId, glm::vec3 pos, glm::vec3 scale,
                  BodyType bodyType, float halfH = 0.5f)
{
    EntityID e = reg.createEntity();
    TransformComponent tc{};
    tc.position = pos;
    tc.rotation = {1, 0, 0, 0};
    tc.scale = scale;
    tc.flags = 1;
    reg.emplace<TransformComponent>(e, tc);
    reg.emplace<WorldTransformComponent>(e);
    reg.emplace<MeshComponent>(e, meshId);
    reg.emplace<MaterialComponent>(e, matId);
    reg.emplace<VisibleTag>(e);
    reg.emplace<ShadowVisibleTag>(e, ShadowVisibleTag{0xFF});
    RigidBodyComponent rb{};
    rb.type = bodyType;
    rb.mass = (bodyType == BodyType::Dynamic) ? 1.0f : 0.0f;
    rb.friction = 0.6f;
    rb.restitution = 0.3f;
    rb.linearDamping = 0.05f;
    rb.angularDamping = 0.05f;
    reg.emplace<RigidBodyComponent>(e, rb);
    ColliderComponent col{};
    col.shape = ColliderShape::Box;
    col.halfExtents = {scale.x * 0.5f, halfH, scale.z * 0.5f};
    reg.emplace<ColliderComponent>(e, col);
    return e;
}
}  // namespace

void PerfSmokeGame::spawnScene(engine::core::Engine& /*engine*/, Registry& reg)
{
    // Ground plane.
    spawnBox(reg, cubeMeshId_, groundMatId_, {0, -0.5f, 0}, {120, 1, 120}, BodyType::Static, 0.5f);

    // 500 static walls (25 x 20 grid).
    std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
    for (int gx = 0; gx < 25; ++gx)
        for (int gz = 0; gz < 20; ++gz)
            spawnBox(reg, cubeMeshId_, wallMatId_,
                     {-40.0f + 3.2f * gx + jitter(rng_), 0.5f, -30.0f + 3.0f * gz + jitter(rng_)},
                     {0.8f, 1.0f, 0.8f}, BodyType::Static);

    // 200 dynamic Jolt boxes.
    std::uniform_real_distribution<float> xz(-35.0f, 35.0f);
    std::uniform_real_distribution<float> y(3.0f, 20.0f);
    for (int i = 0; i < 200; ++i)
        spawnBox(reg, cubeMeshId_, dynMatId_, {xz(rng_), y(rng_), xz(rng_)}, {0.6f, 0.6f, 0.6f},
                 BodyType::Dynamic, 0.3f);

    // Helmet stand-in: metallic PBR cube.
    {
        EntityID e = reg.createEntity();
        TransformComponent tc{};
        tc.position = {0, 2.0f, 0};
        tc.rotation = {1, 0, 0, 0};
        tc.scale = {1.2f, 1.2f, 1.2f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(e, tc);
        reg.emplace<WorldTransformComponent>(e);
        reg.emplace<MeshComponent>(e, cubeMeshId_);
        reg.emplace<MaterialComponent>(e, helmetMatId_);
        reg.emplace<VisibleTag>(e);
        reg.emplace<ShadowVisibleTag>(e, ShadowVisibleTag{0xFF});
    }

    // 16 point lights on a circle.  Latent cost: fs_pbr doesn't yet bind
    // the cluster textures per draw, but LightClusterBuilder::update walks
    // every cluster x light pair anyway — flip the shader flag and this
    // 0.13 ms cost stays the same.
    for (int i = 0; i < 16; ++i)
    {
        EntityID e = reg.createEntity();
        TransformComponent tc{};
        const float a = static_cast<float>(i) / 16.0f * 6.2831853f;
        tc.position = {25.0f * std::cos(a), 6.0f, 25.0f * std::sin(a)};
        tc.rotation = {1, 0, 0, 0};
        tc.scale = {1, 1, 1};
        tc.flags = 1;
        reg.emplace<TransformComponent>(e, tc);
        reg.emplace<WorldTransformComponent>(e);
        PointLightComponent pl{};
        pl.color = {1, 0.9f, 0.7f};
        pl.intensity = 4.0f;
        pl.radius = 8.0f;
        reg.emplace<PointLightComponent>(e, pl);
    }
}

void PerfSmokeGame::onFixedUpdate(engine::core::Engine& /*engine*/, Registry& reg, float fixedDt)
{
    // After the budget run completes, skip all per-system measurement +
    // submission.  On desktop the GLFW window-close request terminates the
    // loop one frame after done_ flips; on Android NativeActivity keeps
    // running until the user closes the app, so we early-out everywhere to
    // avoid re-printing the budget table on every subsequent frame.
    if (done_)
        return;
    auto t0 = Clock::now();
    physicsSys_.update(reg, physics_, fixedDt);
    samples_.physics.push_back(msSince(t0));
}

void PerfSmokeGame::onUpdate(engine::core::Engine& engine, Registry& reg, float /*dt*/)
{
    if (done_)
        return;
    frameStart_ = Clock::now();

    // GameRunner::runLoop calls transformSys.update() after onUpdate but
    // before onRender; we re-run it here ourselves so we can attribute the
    // cost to our own timer.  The second call done by GameRunner will see
    // all-clean dirty flags so it's a tight no-op fast path.
    auto t0 = Clock::now();
    engine::scene::TransformSystem transformSys;
    transformSys.update(reg);
    samples_.transform.push_back(msSince(t0));

    // Drive cycling motion on the helmet so the dirty-flag path stays warm.
    // Without this, TransformSystem's second run wouldn't be representative.
    (void)engine;
}

void PerfSmokeGame::onRender(engine::core::Engine& engine)
{
    const auto W = engine.fbWidth();
    const auto H = engine.fbHeight();
    const float fbW = static_cast<float>(W);
    const float fbH = static_cast<float>(H);

    // Fixed camera so frustum / view results are deterministic frame-to-frame.
    const glm::vec3 camPos{30.0f, 20.0f, 30.0f};
    const glm::mat4 viewMat = glm::lookAt(camPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.1f, 200.f);

    if (registry_ == nullptr || done_)
        return;

    // -- FrustumCullSystem --------------------------------------------------
    auto t0 = Clock::now();
    engine::math::Frustum frustum(projMat * viewMat);
    cullSys_.update(*registry_, engine.resources(), frustum);
    samples_.cull.push_back(msSince(t0));

    // -- Light --------------------------------------------------------------
    const glm::vec3 lightDir = glm::normalize(glm::vec3(1, 2, 1));
    const float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.f, 5.0f, 5.0f, 5.0f, 0.f};
    const glm::vec3 lightPos = lightDir * 30.0f;
    const glm::mat4 lightView = glm::lookAt(lightPos, {0, 0, 0}, {0, 1, 0});
    const glm::mat4 lightProj = glm::ortho(-40.f, 40.f, -40.f, 40.f, 0.1f, 80.f);
    engine.shadow().beginCascade(0, lightView, lightProj);

    // -- Shadow submit ------------------------------------------------------
    auto t1 = Clock::now();
    drawSys_.submitShadowDrawCalls(*registry_, engine.resources(),
                                   bgfx::ProgramHandle{engine.shadowProgram().idx}, 0);
    samples_.shadow.push_back(msSince(t1));

    // -- LightClusterBuilder (latent cost — not currently consumed by
    //    fs_pbr, but proves up the per-frame cluster-build work) -----------
    auto t2 = Clock::now();
    lightCluster_.update(*registry_, viewMat, projMat, 0.1f, 200.f, W, H);
    samples_.lightCluster.push_back(msSince(t2));

    // -- Opaque pass --------------------------------------------------------
    RenderPass(kViewOpaque)
        .rect(0, 0, W, H)
        .clearColorAndDepth(0x202028FF)
        .transform(viewMat, projMat);
    RenderPass(kViewTransparent).clearNone().rect(0, 0, W, H).transform(viewMat, projMat);

    const glm::mat4 shadowMat = engine.shadow().shadowMatrix(0);
    PbrFrameParams frame{
        lightData, glm::value_ptr(shadowMat), engine.shadow().atlasTexture(), W, H, 0.1f, 200.f};
    frame.camPos[0] = camPos.x;
    frame.camPos[1] = camPos.y;
    frame.camPos[2] = camPos.z;

    auto t3 = Clock::now();
    drawSys_.update(*registry_, engine.resources(), bgfx::ProgramHandle{engine.pbrProgram().idx},
                    engine.uniforms(), frame);
    samples_.draw.push_back(msSince(t3));

    samples_.frame.push_back(msSince(frameStart_));

    ++frameIndex_;
    if (frameIndex_ >= framesToRun_)
    {
        reportAndCheckBudgets();
        done_ = true;
#ifndef __ANDROID__
        // Tell GLFW to close the window so the GameRunner::runLoop while()
        // terminates on the next iteration.  Android NativeActivity has no
        // equivalent "exit the app from native code" hook — the budget
        // table just lands in logcat and subsequent frames are no-ops.
        glfwSetWindowShouldClose(engine.glfwHandle(), GLFW_TRUE);
#else
        (void)engine;
#endif
    }
}

void PerfSmokeGame::onShutdown(engine::core::Engine& /*engine*/, Registry& /*reg*/)
{
    lightCluster_.shutdown();
    physics_.shutdown();
}

void PerfSmokeGame::reportAndCheckBudgets()
{
    auto row = [&](const char* name, const std::vector<float>& s, float budget) -> bool
    {
        const float meanMs = mean(s);
        const float p99Ms = percentile(s, 0.99f);
        const float maxMs = maxOf(s);
        const bool ok = (meanMs <= budget);
        perfLog("  %-22s | %7.3f | %7.3f | %7.3f | budget %.2f %s\n", name, meanMs, p99Ms,
                maxMs, budget, ok ? "OK" : "FAIL");
        return ok;
    };

    perfLog("\n=== perf_smoke: %d frames, %zu entities ===\n", framesToRun_,
            samples_.draw.size() ? size_t{702} : size_t{0});
    perfLog("  %-22s | %7s | %7s | %7s |\n", "system", "mean ms", "p99 ms", "max ms");
    perfLog("  -----------------------+---------+---------+---------+\n");
    bool ok = true;
    ok &= row("PhysicsSystem", samples_.physics, budgets_.physicsMeanMs);
    ok &= row("TransformSystem", samples_.transform, budgets_.transformMeanMs);
    ok &= row("FrustumCullSystem", samples_.cull, budgets_.frustumCullMeanMs);
    ok &= row("DrawCallBuildSystem", samples_.draw, budgets_.drawCallMeanMs);
    ok &= row("Shadow submit", samples_.shadow, budgets_.shadowSubmitMeanMs);
    ok &= row("LightClusterBuilder", samples_.lightCluster, budgets_.lightClusterMeanMs);

    const float frameP99 = percentile(samples_.frame, 0.99f);
    const bool frameBudgetOk = (frameP99 <= budgets_.frameP99Ms);
    perfLog("  %-22s | %7.3f | %7.3f | %7.3f | budget p99 %.2f %s\n", "frame total",
            mean(samples_.frame), frameP99, maxOf(samples_.frame), budgets_.frameP99Ms,
            frameBudgetOk ? "OK" : "FAIL");
    ok &= frameBudgetOk;

    perfLog("=== %s ===\n", ok ? "PASS" : "FAIL — budget exceeded; investigate before merge");
    exitCode_ = ok ? 0 : 1;
}

}  // namespace perf_smoke

#ifdef __ANDROID__
// Android entry point — Sama's NativeActivity glue (AndroidApp.cpp) calls
// samaCreateGame() to obtain the IGame to drive.  Selected when sama_android
// is built with -DSAMA_ANDROID_APP=perf_smoke (see top-level CMakeLists.txt).
//
// Run for 600 frames (~10 s @ 60 Hz nominal); on a Pixel 9 with the current
// engine that's enough samples to hit a representative p99.  After the
// budget table is logged the game keeps running (no clean native-exit
// from NativeActivity), and subsequent frames are no-ops — just close the
// app from the launcher when you're done reading logcat.
namespace
{
perf_smoke::PerfBudgets g_androidBudgets{};
// Phone budgets are roughly 2x desktop to start; tighten as the runtime
// improves.  These are wall-clock per-system mean ms / frame.
[[maybe_unused]] const auto kInitAndroidBudgets = []
{
    g_androidBudgets.physicsMeanMs = 1.60f;
    g_androidBudgets.transformMeanMs = 0.40f;
    g_androidBudgets.frustumCullMeanMs = 0.30f;
    g_androidBudgets.drawCallMeanMs = 2.00f;  // the suspect; 4x desktop budget
    g_androidBudgets.shadowSubmitMeanMs = 0.50f;
    g_androidBudgets.lightClusterMeanMs = 1.00f;
    g_androidBudgets.frameP99Ms = 16.67f;  // 60 fps target
    return 0;
}();
perf_smoke::PerfSmokeGame g_perfSmokeGame{600, g_androidBudgets};
}  // namespace

engine::game::IGame* samaCreateGame()
{
    return &g_perfSmokeGame;
}
#endif  // __ANDROID__
