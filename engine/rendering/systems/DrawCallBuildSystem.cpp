#include "engine/rendering/systems/DrawCallBuildSystem.h"

#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

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

            // Upload the world-space transform matrix.
            // bgfx expects a column-major float[16]; GLM Mat4 is column-major.
            bgfx::setTransform(&wtc.matrix[0][0]);

            // Stream 0 — positions.
            bgfx::setVertexBuffer(0, mesh->positionVbh);

            // Stream 1 — surface attributes (optional).
            if (bgfx::isValid(mesh->surfaceVbh))
                bgfx::setVertexBuffer(1, mesh->surfaceVbh);

            // Index buffer.
            bgfx::setIndexBuffer(mesh->ibh);

            // Default render state: depth test write, RGB + alpha write, no culling override.
            bgfx::setState(BGFX_STATE_DEFAULT);

            // Submit to the opaque pass view.
            bgfx::submit(kViewOpaque, program);
        });
}

}  // namespace engine::rendering
