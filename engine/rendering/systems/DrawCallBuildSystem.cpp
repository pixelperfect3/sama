#include "engine/rendering/systems/DrawCallBuildSystem.h"

#include "engine/animation/AnimationComponents.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Helper — binds mesh streams and submits one draw call.
//
// Every overload below acquires a bgfx::Encoder once at function entry and
// releases it at exit.  With bgfx in multi-threaded mode (the Sama default
// since the EngineDesc::singleThreaded flip — see docs/NOTES.md "bgfx
// threading mode"), every implicit-main-encoder set*/submit call grabs the
// bgfx command-list mutex (~1-2 μs on Pixel 5a-class CPUs).  Routing
// through an Encoder makes all calls thread-local: ~50 ns each.  Per
// 100-entity frame that's roughly 1000 mutex grabs eliminated.
// Measurement gate: see docs/PERF_AUDIT_2026-05-25.md item #R1.
// ---------------------------------------------------------------------------

namespace
{

void submitMeshDraw(bgfx::Encoder* enc, const Mesh& mesh, const WorldTransformComponent& wtc,
                    ProgramHandle program)
{
    // Upload the world-space transform matrix.
    // bgfx expects column-major float[16]; GLM Mat4 is column-major.
    enc->setTransform(&wtc.matrix[0][0]);

    // Stream 0 — positions.
    enc->setVertexBuffer(0, mesh.positionVbh);

    // Stream 1 — surface attributes (optional).
    if (bgfx::isValid(mesh.surfaceVbh))
        enc->setVertexBuffer(1, mesh.surfaceVbh);

    // Index buffer.
    enc->setIndexBuffer(mesh.ibh);

    // Default render state: depth test + write, RGB + alpha write, no culling override.
    enc->setState(BGFX_STATE_DEFAULT);

    enc->submit(kViewOpaque, bgfx::ProgramHandle{program.idx});
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Phase 2 — unlit, no material uniforms.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                 ProgramHandle program)
{
    if (!isValid(program))
        return;

    bgfx::Encoder* enc = bgfx::begin();
    if (enc == nullptr)
        return;

    auto visibleView = reg.view<VisibleTag, WorldTransformComponent, MeshComponent>();

    visibleView.each(
        [&](ecs::EntityID /*entity*/, const VisibleTag& /*tag*/, const WorldTransformComponent& wtc,
            const MeshComponent& mc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            submitMeshDraw(enc, *mesh, wtc, program);
        });

    bgfx::end(enc);
}

// ---------------------------------------------------------------------------
// Phase 3 — PBR, uploads per-draw u_material uniform from MaterialComponent.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                 ProgramHandle program, ShaderUniforms* uniforms)
{
    if (!isValid(program))
        return;

    // Fall back to the unlit path when no uniforms object is provided.
    if (uniforms == nullptr)
    {
        update(reg, res, program);
        return;
    }

    bgfx::Encoder* enc = bgfx::begin();
    if (enc == nullptr)
        return;

    // Iterate entities that have all three required components.
    auto visibleView =
        reg.view<VisibleTag, WorldTransformComponent, MeshComponent, MaterialComponent>();

    const Material defaultMat{};

    visibleView.each(
        [&](ecs::EntityID /*entity*/, const VisibleTag& /*tag*/, const WorldTransformComponent& wtc,
            const MeshComponent& mc, const MaterialComponent& matc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            // Fetch material; fall back to default if not found.
            const Material* mat = res.getMaterial(matc.material);
            if (mat == nullptr)
                mat = &defaultMat;

            // Pack material parameters into two vec4s:
            //   [0] = {albedo.r, albedo.g, albedo.b, roughness}
            //   [1] = {metallic, emissiveScale, 0, 0}
            float materialData[8] = {
                mat->albedo.x, mat->albedo.y,      mat->albedo.z, mat->roughness,
                mat->metallic, mat->emissiveScale, mat->albedo.w, 0.0f,
            };
            enc->setUniform(uniforms->u_material, materialData, 2);

            submitMeshDraw(enc, *mesh, wtc, program);
        });

    bgfx::end(enc);
}

// ---------------------------------------------------------------------------
// Phase 3 — PBR + directional shadow.
// Sets all per-draw state: material, light, shadow matrix, all textures.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                 ProgramHandle program, const ShaderUniforms& uniforms,
                                 const PbrFrameParams& frame)
{
    if (!isValid(program))
        return;

    bgfx::Encoder* enc = bgfx::begin();
    if (enc == nullptr)
        return;

    bgfx::TextureHandle whiteTex = bgfx::TextureHandle{res.whiteTexture().idx};
    bgfx::TextureHandle whiteCubeTex = bgfx::TextureHandle{res.whiteCubeTexture().idx};
    // Neutral normal map: (128, 128, 255) → tangent-space (0, 0, 1) → N = Ngeom.
    // White (255, 255, 255) decodes to (1, 1, 1) which distorts the normal.
    bgfx::TextureHandle normalFallback =
        bgfx::isValid(bgfx::TextureHandle{res.neutralNormalTexture().idx})
            ? bgfx::TextureHandle{res.neutralNormalTexture().idx}
            : whiteTex;

    // Frame-level uniforms — must be re-uploaded before every submit() because
    // bgfx resets all per-draw state after each submit().  Hoisting these
    // calls outside the loop with BGFX_DISCARD_NONE submit flags was
    // considered (see docs/PERF_AUDIT_2026-05-25.md item #R1) but rejected:
    // bgfx captures [m_uniformBegin, m_uniformEnd] per draw, so a "preserved"
    // range grows unboundedly across the loop and the backend re-walks the
    // entire range per submit — O(N²) GPU-side uniform processing for an N-
    // entity frame.  The actual CPU win comes from routing every set*/submit
    // through this Encoder instead of the implicit main-encoder path (~1-2 μs
    // mutex grab → ~50 ns thread-local recording).
    const float frameW = static_cast<float>(frame.viewportW);
    const float frameH = static_cast<float>(frame.viewportH);

    // u_frameParams[0] = {viewportW, viewportH, near, far}
    // u_frameParams[1] = {camPos.x, camPos.y, camPos.z, 0} — camera world position for V vector
    const float frameParamsData[8] = {
        frameW,          frameH,          frame.nearPlane, frame.farPlane,
        frame.camPos[0], frame.camPos[1], frame.camPos[2], 0.0f};

    // u_lightParams = {numLights, screenW, screenH, 0}
    // numLights=0 disables the clustered loop; screenW/H prevent tile div-by-zero.
    const float lightParamsData[4] = {0.0f, frameW, frameH, 0.0f};

    // u_iblParams = {maxMipLevels, iblEnabled, 0, 0}
    const float iblParamsData[4] = {frame.iblEnabled ? frame.maxMipLevels : 0.0f,
                                    frame.iblEnabled ? 1.0f : 0.0f, 0.0f, 0.0f};

    // Resolve IBL textures once — the active triple is constant for the loop.
    const bool iblActive = frame.iblEnabled && bgfx::isValid(frame.brdfLut);
    const bgfx::TextureHandle cubeFallback = bgfx::isValid(whiteCubeTex) ? whiteCubeTex : whiteTex;
    const bgfx::TextureHandle iblIrradiance = iblActive ? frame.irradiance : cubeFallback;
    const bgfx::TextureHandle iblPrefiltered = iblActive ? frame.prefiltered : cubeFallback;
    const bgfx::TextureHandle iblBrdfLut = iblActive ? frame.brdfLut : whiteTex;
    const bool hasShadowAtlas = bgfx::isValid(frame.shadowAtlas);

    const Material defaultMat2{};

    auto visibleView =
        reg.view<VisibleTag, WorldTransformComponent, MeshComponent, MaterialComponent>();

    // Frame-constant texture bindings — audit "Rendering" §:
    // slots 5/6/7/8 (shadow + IBL triple) and 12/13/14 (cluster light data)
    // never change across the visible loop.  Bind them ONCE here, then
    // submit each draw with BGFX_DISCARD_ALL & ~BGFX_DISCARD_BINDINGS so
    // bgfx preserves the binding state across submits (verified in
    // bgfx_p.h:1825 — RenderBind::clear is gated on BGFX_DISCARD_BINDINGS).
    // The per-material slots (0..4) are still rebound every iteration; the
    // discard mask preserves THEIR bindings too across submits, but we
    // overwrite them with the next material's textures so they are correct
    // per draw.  State / transform / vertex+index buffers / uniforms still
    // get the standard per-draw discard so each draw constructs its own
    // RenderDraw from scratch.
    if (hasShadowAtlas)
        enc->setTexture(5, uniforms.s_shadowMap, frame.shadowAtlas);
    enc->setTexture(6, uniforms.s_irradiance, iblIrradiance);
    enc->setTexture(7, uniforms.s_prefiltered, iblPrefiltered);
    enc->setTexture(8, uniforms.s_brdfLut, iblBrdfLut);
    enc->setTexture(12, uniforms.s_lightData, whiteTex);
    enc->setTexture(13, uniforms.s_lightGrid, whiteTex);
    enc->setTexture(14, uniforms.s_lightIndex, whiteTex);

    constexpr uint8_t kPreserveBindings =
        static_cast<uint8_t>(BGFX_DISCARD_ALL & ~BGFX_DISCARD_BINDINGS);

    visibleView.each(
        [&](ecs::EntityID entity, const VisibleTag& /*tag*/, const WorldTransformComponent& wtc,
            const MeshComponent& mc, const MaterialComponent& matc)
        {
            // Skip skinned entities — rendered separately by updateSkinned().
            if (reg.has<animation::SkinComponent>(entity))
                return;

            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            const Material* mat = res.getMaterial(matc.material);
            if (mat == nullptr)
                mat = &defaultMat2;

            // Per-draw: material
            float materialData[8] = {
                mat->albedo.x, mat->albedo.y,      mat->albedo.z, mat->roughness,
                mat->metallic, mat->emissiveScale, mat->albedo.w, 0.0f,
            };
            enc->setUniform(uniforms.u_material, materialData, 2);

            // Per-draw: directional light + shadow matrix
            enc->setUniform(uniforms.u_dirLight, frame.lightData, 2);
            enc->setUniform(uniforms.u_shadowMatrix, frame.shadowMatrix, 4);

            // Per-draw: frame / cluster / IBL uniforms
            enc->setUniform(uniforms.u_frameParams, frameParamsData, 2);
            enc->setUniform(uniforms.u_lightParams, lightParamsData);
            enc->setUniform(uniforms.u_iblParams, iblParamsData);

            // Per-material texture slots only — frame-constant slots
            // (5, 6, 7, 8, 12, 13, 14) were bound once before the loop and
            // persist via the kPreserveBindings discard mask below.
            // Material texture IDs reference RenderResources; fall back to
            // whiteTex when not set so Metal validation never sees a nil argument.
            auto resolveOrWhite = [&](uint32_t texId) -> bgfx::TextureHandle
            {
                if (texId != 0)
                {
                    bgfx::TextureHandle t = bgfx::TextureHandle{res.getTexture(texId).idx};
                    if (bgfx::isValid(t))
                        return t;
                }
                return whiteTex;
            };
            enc->setTexture(0, uniforms.s_albedo, resolveOrWhite(mat->albedoMapId));
            enc->setTexture(1, uniforms.s_normal,
                            mat->normalMapId ? resolveOrWhite(mat->normalMapId) : normalFallback);
            enc->setTexture(2, uniforms.s_orm, resolveOrWhite(mat->ormMapId));
            enc->setTexture(3, uniforms.s_emissive, resolveOrWhite(mat->emissiveMapId));
            enc->setTexture(4, uniforms.s_occlusion, resolveOrWhite(mat->occlusionMapId));

            enc->setTransform(&wtc.matrix[0][0]);
            enc->setVertexBuffer(0, mesh->positionVbh);
            if (bgfx::isValid(mesh->surfaceVbh))
                enc->setVertexBuffer(1, mesh->surfaceVbh);
            enc->setIndexBuffer(mesh->ibh);

            // Transparent materials: alpha-blend, no depth write, submit to
            // transparent view (rendered after opaque, depth-tested against opaque).
            if (mat->transparent)
            {
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                              BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA |
                              BGFX_STATE_BLEND_ALPHA);
                enc->submit(kViewTransparent, bgfx::ProgramHandle{program.idx}, 0,
                            kPreserveBindings);
            }
            else
            {
                enc->setState(BGFX_STATE_DEFAULT);
                enc->submit(kViewOpaque, bgfx::ProgramHandle{program.idx}, 0, kPreserveBindings);
            }
        });

    bgfx::end(enc);
}

// ---------------------------------------------------------------------------
// Phase 4 — depth-only shadow draw calls.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::submitShadowDrawCalls(ecs::Registry& reg, const RenderResources& res,
                                                ProgramHandle shadowProgram, uint32_t cascadeIndex)
{
    if (!isValid(shadowProgram))
        return;

    bgfx::Encoder* enc = bgfx::begin();
    if (enc == nullptr)
        return;

    const uint8_t cascadeBit = static_cast<uint8_t>(1u << cascadeIndex);
    const bgfx::ViewId shadowView = static_cast<bgfx::ViewId>(kViewShadowBase + cascadeIndex);

    auto shadowView_ = reg.view<ShadowVisibleTag, WorldTransformComponent, MeshComponent>();

    shadowView_.each(
        [&](ecs::EntityID entity, const ShadowVisibleTag& tag, const WorldTransformComponent& wtc,
            const MeshComponent& mc)
        {
            if (!(tag.cascadeMask & cascadeBit))
                return;

            // Skip skinned entities — rendered by submitSkinnedShadowDrawCalls.
            if (reg.has<animation::SkinComponent>(entity))
                return;

            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !bgfx::isValid(mesh->positionVbh) || !bgfx::isValid(mesh->ibh))
                return;

            enc->setTransform(&wtc.matrix[0][0]);

            // Stream 0 only — no surface attributes needed for depth-only pass.
            enc->setVertexBuffer(0, mesh->positionVbh);
            enc->setIndexBuffer(mesh->ibh);

            // Depth write + depth test + cull back faces (CW) so front faces
            // facing the light write correct shadow depth.
            enc->setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);

            enc->submit(shadowView, bgfx::ProgramHandle{shadowProgram.idx});
        });

    bgfx::end(enc);
}

// ---------------------------------------------------------------------------
// Skinned shadow pass — depth-only with bone matrices.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::submitSkinnedShadowDrawCalls(ecs::Registry& reg,
                                                       const RenderResources& res,
                                                       ProgramHandle skinnedShadowProgram,
                                                       uint32_t cascadeIndex,
                                                       const math::Mat4* boneBuffer)
{
    if (!isValid(skinnedShadowProgram) || !boneBuffer)
        return;

    bgfx::Encoder* enc = bgfx::begin();
    if (enc == nullptr)
        return;

    const uint8_t cascadeBit = static_cast<uint8_t>(1u << cascadeIndex);
    const bgfx::ViewId shadowView = static_cast<bgfx::ViewId>(kViewShadowBase + cascadeIndex);

    auto skinnedShadowView = reg.view<ShadowVisibleTag, animation::SkinComponent, MeshComponent>();

    skinnedShadowView.each(
        [&](ecs::EntityID /*entity*/, const ShadowVisibleTag& tag,
            const animation::SkinComponent& skin, const MeshComponent& mc)
        {
            if (!(tag.cascadeMask & cascadeBit))
                return;

            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !bgfx::isValid(mesh->positionVbh) || !bgfx::isValid(mesh->ibh))
                return;

            // Upload bone matrices (already in world space from AnimationSystem).
            const math::Mat4* bones = boneBuffer + skin.boneMatrixOffset;
            enc->setTransform(&bones[0][0][0], skin.boneCount);

            enc->setVertexBuffer(0, mesh->positionVbh);
            // Bind surface stream even though shadow shader doesn't use it —
            // ensures stream indices are contiguous for Metal backend.
            if (bgfx::isValid(mesh->surfaceVbh))
                enc->setVertexBuffer(1, mesh->surfaceVbh);
            if (bgfx::isValid(mesh->skinningVbh))
                enc->setVertexBuffer(2, mesh->skinningVbh);
            enc->setIndexBuffer(mesh->ibh);

            enc->setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);

            enc->submit(shadowView, bgfx::ProgramHandle{skinnedShadowProgram.idx});
        });

    bgfx::end(enc);
}

// ---------------------------------------------------------------------------
// Skinned PBR draw calls — entities with SkinComponent.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::updateSkinned(ecs::Registry& reg, const RenderResources& res,
                                        ProgramHandle skinnedProgram,
                                        const ShaderUniforms& uniforms, const PbrFrameParams& frame,
                                        const math::Mat4* boneBuffer)
{
    if (!isValid(skinnedProgram) || !boneBuffer)
        return;

    bgfx::Encoder* enc = bgfx::begin();
    if (enc == nullptr)
        return;

    bgfx::TextureHandle whiteTex = bgfx::TextureHandle{res.whiteTexture().idx};
    bgfx::TextureHandle whiteCubeTex = bgfx::TextureHandle{res.whiteCubeTexture().idx};
    bgfx::TextureHandle normalFallback =
        bgfx::isValid(bgfx::TextureHandle{res.neutralNormalTexture().idx})
            ? bgfx::TextureHandle{res.neutralNormalTexture().idx}
            : whiteTex;

    const float frameW = static_cast<float>(frame.viewportW);
    const float frameH = static_cast<float>(frame.viewportH);

    const float frameParamsData[8] = {
        frameW,          frameH,          frame.nearPlane, frame.farPlane,
        frame.camPos[0], frame.camPos[1], frame.camPos[2], 0.0f};

    const float lightParamsData[4] = {0.0f, frameW, frameH, 0.0f};
    const float iblParamsData[4] = {frame.iblEnabled ? frame.maxMipLevels : 0.0f,
                                    frame.iblEnabled ? 1.0f : 0.0f, 0.0f, 0.0f};

    // Resolve IBL textures once — see the static-mesh overload for the
    // rationale behind hoisting these (cube fallback when IBL disabled).
    const bool iblActive = frame.iblEnabled && bgfx::isValid(frame.brdfLut);
    const bgfx::TextureHandle cubeFallback = bgfx::isValid(whiteCubeTex) ? whiteCubeTex : whiteTex;
    const bgfx::TextureHandle iblIrradiance = iblActive ? frame.irradiance : cubeFallback;
    const bgfx::TextureHandle iblPrefiltered = iblActive ? frame.prefiltered : cubeFallback;
    const bgfx::TextureHandle iblBrdfLut = iblActive ? frame.brdfLut : whiteTex;
    const bool hasShadowAtlas = bgfx::isValid(frame.shadowAtlas);

    const Material skinnedDefaultMat{};

    auto skinnedView =
        reg.view<VisibleTag, animation::SkinComponent, MeshComponent, MaterialComponent>();

    // Frame-constant texture bindings — see the static-mesh update() above
    // for the full rationale.  Bound once before the loop; persist via
    // kPreserveBindings discard mask on each submit().
    if (hasShadowAtlas)
        enc->setTexture(5, uniforms.s_shadowMap, frame.shadowAtlas);
    enc->setTexture(6, uniforms.s_irradiance, iblIrradiance);
    enc->setTexture(7, uniforms.s_prefiltered, iblPrefiltered);
    enc->setTexture(8, uniforms.s_brdfLut, iblBrdfLut);
    enc->setTexture(12, uniforms.s_lightData, whiteTex);
    enc->setTexture(13, uniforms.s_lightGrid, whiteTex);
    enc->setTexture(14, uniforms.s_lightIndex, whiteTex);

    constexpr uint8_t kPreserveBindings =
        static_cast<uint8_t>(BGFX_DISCARD_ALL & ~BGFX_DISCARD_BINDINGS);

    skinnedView.each(
        [&](ecs::EntityID /*entity*/, const VisibleTag& /*tag*/,
            const animation::SkinComponent& skin, const MeshComponent& mc,
            const MaterialComponent& matc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            const Material* mat = res.getMaterial(matc.material);
            if (mat == nullptr)
                mat = &skinnedDefaultMat;

            // Per-draw: material
            float materialData[8] = {
                mat->albedo.x, mat->albedo.y,      mat->albedo.z, mat->roughness,
                mat->metallic, mat->emissiveScale, mat->albedo.w, 0.0f,
            };
            enc->setUniform(uniforms.u_material, materialData, 2);

            // Per-draw: directional light + shadow matrix
            enc->setUniform(uniforms.u_dirLight, frame.lightData, 2);
            enc->setUniform(uniforms.u_shadowMatrix, frame.shadowMatrix, 4);

            // Per-draw: frame / cluster / IBL uniforms
            enc->setUniform(uniforms.u_frameParams, frameParamsData, 2);
            enc->setUniform(uniforms.u_lightParams, lightParamsData);
            enc->setUniform(uniforms.u_iblParams, iblParamsData);

            // Per-material texture slots only — frame-constant slots
            // (5/6/7/8/12/13/14) bound once before the loop.
            auto resolveOrWhite = [&](uint32_t texId) -> bgfx::TextureHandle
            {
                if (texId != 0)
                {
                    bgfx::TextureHandle t = bgfx::TextureHandle{res.getTexture(texId).idx};
                    if (bgfx::isValid(t))
                        return t;
                }
                return whiteTex;
            };
            enc->setTexture(0, uniforms.s_albedo, resolveOrWhite(mat->albedoMapId));
            enc->setTexture(1, uniforms.s_normal,
                            mat->normalMapId ? resolveOrWhite(mat->normalMapId) : normalFallback);
            enc->setTexture(2, uniforms.s_orm, resolveOrWhite(mat->ormMapId));
            enc->setTexture(3, uniforms.s_emissive, resolveOrWhite(mat->emissiveMapId));
            enc->setTexture(4, uniforms.s_occlusion, resolveOrWhite(mat->occlusionMapId));

            // Upload bone matrices via setTransform with count > 1.
            const math::Mat4* bones = boneBuffer + skin.boneMatrixOffset;
            enc->setTransform(&bones[0][0][0], skin.boneCount);

            // Bind vertex streams.
            enc->setVertexBuffer(0, mesh->positionVbh);
            if (bgfx::isValid(mesh->surfaceVbh))
                enc->setVertexBuffer(1, mesh->surfaceVbh);
            if (bgfx::isValid(mesh->skinningVbh))
                enc->setVertexBuffer(2, mesh->skinningVbh);
            enc->setIndexBuffer(mesh->ibh);

            if (mat->transparent)
            {
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                              BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA |
                              BGFX_STATE_BLEND_ALPHA);
                enc->submit(kViewTransparent, bgfx::ProgramHandle{skinnedProgram.idx}, 0,
                            kPreserveBindings);
            }
            else
            {
                enc->setState(BGFX_STATE_DEFAULT);
                enc->submit(kViewOpaque, bgfx::ProgramHandle{skinnedProgram.idx}, 0,
                            kPreserveBindings);
            }
        });

    bgfx::end(enc);
}

}  // namespace engine::rendering
