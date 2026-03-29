#include "engine/rendering/systems/DrawCallBuildSystem.h"

#include "engine/animation/AnimationComponents.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Helper — binds mesh streams and submits one draw call.
// ---------------------------------------------------------------------------

namespace
{

void submitMeshDraw(const Mesh& mesh, const WorldTransformComponent& wtc,
                    bgfx::ProgramHandle program)
{
    // Upload the world-space transform matrix.
    // bgfx expects column-major float[16]; GLM Mat4 is column-major.
    bgfx::setTransform(&wtc.matrix[0][0]);

    // Stream 0 — positions.
    bgfx::setVertexBuffer(0, mesh.positionVbh);

    // Stream 1 — surface attributes (optional).
    if (bgfx::isValid(mesh.surfaceVbh))
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);

    // Index buffer.
    bgfx::setIndexBuffer(mesh.ibh);

    // Default render state: depth test + write, RGB + alpha write, no culling override.
    bgfx::setState(BGFX_STATE_DEFAULT);

    bgfx::submit(kViewOpaque, program);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Phase 2 — unlit, no material uniforms.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                 bgfx::ProgramHandle program)
{
    if (!bgfx::isValid(program))
        return;

    auto visibleView = reg.view<VisibleTag, WorldTransformComponent, MeshComponent>();

    visibleView.each(
        [&](ecs::EntityID /*entity*/, const VisibleTag& /*tag*/, const WorldTransformComponent& wtc,
            const MeshComponent& mc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            submitMeshDraw(*mesh, wtc, program);
        });
}

// ---------------------------------------------------------------------------
// Phase 3 — PBR, uploads per-draw u_material uniform from MaterialComponent.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                 bgfx::ProgramHandle program, ShaderUniforms* uniforms)
{
    if (!bgfx::isValid(program))
        return;

    // Fall back to the unlit path when no uniforms object is provided.
    if (uniforms == nullptr)
    {
        update(reg, res, program);
        return;
    }

    // Iterate entities that have all three required components.
    auto visibleView =
        reg.view<VisibleTag, WorldTransformComponent, MeshComponent, MaterialComponent>();

    visibleView.each(
        [&](ecs::EntityID /*entity*/, const VisibleTag& /*tag*/, const WorldTransformComponent& wtc,
            const MeshComponent& mc, const MaterialComponent& matc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            // Fetch material; fall back to default if not found.
            const Material* mat = res.getMaterial(matc.material);
            Material defaultMat{};
            if (mat == nullptr)
                mat = &defaultMat;

            // Pack material parameters into two vec4s:
            //   [0] = {albedo.r, albedo.g, albedo.b, roughness}
            //   [1] = {metallic, emissiveScale, 0, 0}
            float materialData[8] = {
                mat->albedo.x, mat->albedo.y,      mat->albedo.z, mat->roughness,
                mat->metallic, mat->emissiveScale, 0.0f,          0.0f,
            };
            bgfx::setUniform(uniforms->u_material, materialData, 2);

            submitMeshDraw(*mesh, wtc, program);
        });
}

// ---------------------------------------------------------------------------
// Phase 3 — PBR + directional shadow.
// Sets all per-draw state: material, light, shadow matrix, all textures.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                 bgfx::ProgramHandle program, const ShaderUniforms& uniforms,
                                 const PbrFrameParams& frame)
{
    if (!bgfx::isValid(program))
        return;

    bgfx::TextureHandle whiteTex = res.whiteTexture();
    bgfx::TextureHandle whiteCubeTex = res.whiteCubeTexture();
    // Neutral normal map: (128, 128, 255) → tangent-space (0, 0, 1) → N = Ngeom.
    // White (255, 255, 255) decodes to (1, 1, 1) which distorts the normal.
    bgfx::TextureHandle normalFallback =
        bgfx::isValid(res.neutralNormalTexture()) ? res.neutralNormalTexture() : whiteTex;

    // Frame-level uniforms — must be re-uploaded before every submit() because
    // bgfx resets all per-draw state after each submit().
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

    // u_iblParams = {maxMipLevels, iblEnabled=0, 0, 0} — IBL disabled, uses flat ambient.
    const float iblParamsData[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    auto visibleView =
        reg.view<VisibleTag, WorldTransformComponent, MeshComponent, MaterialComponent>();

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
            Material defaultMat{};
            if (mat == nullptr)
                mat = &defaultMat;

            // Per-draw: material
            float materialData[8] = {
                mat->albedo.x, mat->albedo.y,      mat->albedo.z, mat->roughness,
                mat->metallic, mat->emissiveScale, 0.0f,          0.0f,
            };
            bgfx::setUniform(uniforms.u_material, materialData, 2);

            // Per-draw: directional light + shadow matrix
            bgfx::setUniform(uniforms.u_dirLight, frame.lightData, 2);
            bgfx::setUniform(uniforms.u_shadowMatrix, frame.shadowMatrix, 4);

            // Per-draw: frame / cluster / IBL uniforms
            bgfx::setUniform(uniforms.u_frameParams, frameParamsData, 2);
            bgfx::setUniform(uniforms.u_lightParams, lightParamsData);
            bgfx::setUniform(uniforms.u_iblParams, iblParamsData);

            // Per-draw: bind all texture slots declared in fs_pbr.sc.
            // Material texture IDs reference RenderResources; fall back to
            // whiteTex when not set so Metal validation never sees a nil argument.
            auto resolveOrWhite = [&](uint32_t texId) -> bgfx::TextureHandle
            {
                if (texId != 0)
                {
                    bgfx::TextureHandle t = res.getTexture(texId);
                    if (bgfx::isValid(t))
                        return t;
                }
                return whiteTex;
            };
            bgfx::setTexture(0, uniforms.s_albedo, resolveOrWhite(mat->albedoMapId));
            bgfx::setTexture(1, uniforms.s_normal,
                             mat->normalMapId ? resolveOrWhite(mat->normalMapId) : normalFallback);
            bgfx::setTexture(2, uniforms.s_orm, resolveOrWhite(mat->ormMapId));
            bgfx::setTexture(3, uniforms.s_emissive, resolveOrWhite(mat->emissiveMapId));
            bgfx::setTexture(4, uniforms.s_occlusion, resolveOrWhite(mat->occlusionMapId));
            bgfx::setTexture(8, uniforms.s_brdfLut, whiteTex);
            bgfx::setTexture(12, uniforms.s_lightData, whiteTex);
            bgfx::setTexture(13, uniforms.s_lightGrid, whiteTex);
            bgfx::setTexture(14, uniforms.s_lightIndex, whiteTex);
            if (bgfx::isValid(frame.shadowAtlas))
                bgfx::setTexture(5, uniforms.s_shadowMap, frame.shadowAtlas);
            {
                bgfx::TextureHandle cube = bgfx::isValid(whiteCubeTex) ? whiteCubeTex : whiteTex;
                bgfx::setTexture(6, uniforms.s_irradiance, cube);
                bgfx::setTexture(7, uniforms.s_prefiltered, cube);
            }

            bgfx::setTransform(&wtc.matrix[0][0]);
            bgfx::setVertexBuffer(0, mesh->positionVbh);
            if (bgfx::isValid(mesh->surfaceVbh))
                bgfx::setVertexBuffer(1, mesh->surfaceVbh);
            bgfx::setIndexBuffer(mesh->ibh);
            bgfx::setState(BGFX_STATE_DEFAULT);
            bgfx::submit(kViewOpaque, program);
        });
}

// ---------------------------------------------------------------------------
// Phase 4 — depth-only shadow draw calls.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::submitShadowDrawCalls(ecs::Registry& reg, const RenderResources& res,
                                                bgfx::ProgramHandle shadowProgram,
                                                uint32_t cascadeIndex)
{
    if (!bgfx::isValid(shadowProgram))
        return;

    const uint8_t cascadeBit = static_cast<uint8_t>(1u << cascadeIndex);
    const bgfx::ViewId shadowView = static_cast<bgfx::ViewId>(kViewShadowBase + cascadeIndex);

    auto shadowView_ = reg.view<ShadowVisibleTag, WorldTransformComponent, MeshComponent>();

    shadowView_.each(
        [&](ecs::EntityID /*entity*/, const ShadowVisibleTag& tag,
            const WorldTransformComponent& wtc, const MeshComponent& mc)
        {
            if (!(tag.cascadeMask & cascadeBit))
                return;

            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !bgfx::isValid(mesh->positionVbh) || !bgfx::isValid(mesh->ibh))
                return;

            bgfx::setTransform(&wtc.matrix[0][0]);

            // Stream 0 only — no surface attributes needed for depth-only pass.
            bgfx::setVertexBuffer(0, mesh->positionVbh);
            bgfx::setIndexBuffer(mesh->ibh);

            // Depth write + depth test + cull back faces (CW) so front faces
            // facing the light write correct shadow depth.
            bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);

            bgfx::submit(shadowView, shadowProgram);
        });
}

// ---------------------------------------------------------------------------
// Skinned PBR draw calls — entities with SkinComponent.
// ---------------------------------------------------------------------------

void DrawCallBuildSystem::updateSkinned(ecs::Registry& reg, const RenderResources& res,
                                        bgfx::ProgramHandle skinnedProgram,
                                        const ShaderUniforms& uniforms,
                                        const PbrFrameParams& frame,
                                        const math::Mat4* boneBuffer)
{
    if (!bgfx::isValid(skinnedProgram) || !boneBuffer)
        return;

    bgfx::TextureHandle whiteTex = res.whiteTexture();
    bgfx::TextureHandle whiteCubeTex = res.whiteCubeTexture();
    bgfx::TextureHandle normalFallback =
        bgfx::isValid(res.neutralNormalTexture()) ? res.neutralNormalTexture() : whiteTex;

    const float frameW = static_cast<float>(frame.viewportW);
    const float frameH = static_cast<float>(frame.viewportH);

    const float frameParamsData[8] = {
        frameW,          frameH,          frame.nearPlane, frame.farPlane,
        frame.camPos[0], frame.camPos[1], frame.camPos[2], 0.0f};

    const float lightParamsData[4] = {0.0f, frameW, frameH, 0.0f};
    const float iblParamsData[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    auto skinnedView = reg.view<VisibleTag, animation::SkinComponent, MeshComponent,
                                MaterialComponent>();

    skinnedView.each(
        [&](ecs::EntityID /*entity*/, const VisibleTag& /*tag*/,
            const animation::SkinComponent& skin, const MeshComponent& mc,
            const MaterialComponent& matc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh || !mesh->isValid())
                return;

            const Material* mat = res.getMaterial(matc.material);
            Material defaultMat{};
            if (mat == nullptr)
                mat = &defaultMat;

            // Per-draw: material
            float materialData[8] = {
                mat->albedo.x, mat->albedo.y,      mat->albedo.z, mat->roughness,
                mat->metallic, mat->emissiveScale, 0.0f,          0.0f,
            };
            bgfx::setUniform(uniforms.u_material, materialData, 2);

            // Per-draw: directional light + shadow matrix
            bgfx::setUniform(uniforms.u_dirLight, frame.lightData, 2);
            bgfx::setUniform(uniforms.u_shadowMatrix, frame.shadowMatrix, 4);

            // Per-draw: frame / cluster / IBL uniforms
            bgfx::setUniform(uniforms.u_frameParams, frameParamsData, 2);
            bgfx::setUniform(uniforms.u_lightParams, lightParamsData);
            bgfx::setUniform(uniforms.u_iblParams, iblParamsData);

            // Per-draw: textures
            auto resolveOrWhite = [&](uint32_t texId) -> bgfx::TextureHandle
            {
                if (texId != 0)
                {
                    bgfx::TextureHandle t = res.getTexture(texId);
                    if (bgfx::isValid(t))
                        return t;
                }
                return whiteTex;
            };
            bgfx::setTexture(0, uniforms.s_albedo, resolveOrWhite(mat->albedoMapId));
            bgfx::setTexture(1, uniforms.s_normal,
                             mat->normalMapId ? resolveOrWhite(mat->normalMapId) : normalFallback);
            bgfx::setTexture(2, uniforms.s_orm, resolveOrWhite(mat->ormMapId));
            bgfx::setTexture(3, uniforms.s_emissive, resolveOrWhite(mat->emissiveMapId));
            bgfx::setTexture(4, uniforms.s_occlusion, resolveOrWhite(mat->occlusionMapId));
            bgfx::setTexture(8, uniforms.s_brdfLut, whiteTex);
            bgfx::setTexture(12, uniforms.s_lightData, whiteTex);
            bgfx::setTexture(13, uniforms.s_lightGrid, whiteTex);
            bgfx::setTexture(14, uniforms.s_lightIndex, whiteTex);
            if (bgfx::isValid(frame.shadowAtlas))
                bgfx::setTexture(5, uniforms.s_shadowMap, frame.shadowAtlas);
            {
                bgfx::TextureHandle cube = bgfx::isValid(whiteCubeTex) ? whiteCubeTex : whiteTex;
                bgfx::setTexture(6, uniforms.s_irradiance, cube);
                bgfx::setTexture(7, uniforms.s_prefiltered, cube);
            }

            // Upload bone matrices via bgfx::setTransform with count > 1.
            const math::Mat4* bones = boneBuffer + skin.boneMatrixOffset;
            bgfx::setTransform(&bones[0][0][0], skin.boneCount);

            // Bind vertex streams.
            bgfx::setVertexBuffer(0, mesh->positionVbh);
            if (bgfx::isValid(mesh->surfaceVbh))
                bgfx::setVertexBuffer(1, mesh->surfaceVbh);
            if (bgfx::isValid(mesh->skinningVbh))
                bgfx::setVertexBuffer(2, mesh->skinningVbh);
            bgfx::setIndexBuffer(mesh->ibh);
            bgfx::setState(BGFX_STATE_DEFAULT);
            bgfx::submit(kViewOpaque, skinnedProgram);
        });
}

}  // namespace engine::rendering
