#include "engine/rendering/systems/DrawCallBuildSystem.h"

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

}  // namespace engine::rendering
