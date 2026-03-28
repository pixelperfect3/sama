#include "engine/rendering/systems/InstanceBufferBuildSystem.h"

#include <ankerl/unordered_dense.h>

#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

namespace
{

// One entry collected during the grouping pass.
struct InstanceEntry
{
    math::Mat4 worldMatrix;
    bool visible;  // entity has VisibleTag
};

// Submit one instanced draw call for a group using bgfx::allocInstanceDataBuffer.
// Caller guarantees numInstances > 0 and mesh is valid.
void submitInstanced(const Mesh& mesh, const std::vector<InstanceEntry>& entries,
                     bgfx::Encoder* enc, bgfx::ProgramHandle program)
{
    const auto numInstances = static_cast<uint32_t>(entries.size());
    constexpr uint16_t kInstanceStride = 64;  // one Mat4 = 16 floats = 64 bytes

    bgfx::InstanceDataBuffer idb{};
    bgfx::allocInstanceDataBuffer(&idb, numInstances, kInstanceStride);

    auto* dest = reinterpret_cast<float*>(idb.data);
    for (const InstanceEntry& e : entries)
    {
        std::memcpy(dest, glm::value_ptr(e.worldMatrix), 64);
        dest += 16;
    }

    enc->setVertexBuffer(0, mesh.positionVbh);
    if (bgfx::isValid(mesh.surfaceVbh))
        enc->setVertexBuffer(1, mesh.surfaceVbh);
    enc->setIndexBuffer(mesh.ibh);
    enc->setInstanceDataBuffer(&idb);
    enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                  BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW);
    enc->submit(kViewOpaque, program);
}

// Fallback: submit one non-instanced draw call per entry.
// Used when BGFX_CAPS_INSTANCING is unavailable.
void submitFallback(const Mesh& mesh, const std::vector<InstanceEntry>& entries,
                    bgfx::ProgramHandle program)
{
    for (const InstanceEntry& e : entries)
    {
        bgfx::setTransform(glm::value_ptr(e.worldMatrix));
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        if (bgfx::isValid(mesh.surfaceVbh))
            bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW);
        bgfx::submit(kViewOpaque, program);
    }
}

}  // anonymous namespace

void InstanceBufferBuildSystem::update(ecs::Registry& reg, const RenderResources& res,
                                       bgfx::ProgramHandle program)
{
    if (!bgfx::isValid(program))
        return;

    // -----------------------------------------------------------------------
    // Pass 1 — group entities by (instanceGroupId, meshId).
    // Key: instanceGroupId.  We also record the meshId from the first entity in
    // each group (all entities sharing a group are expected to share a mesh).
    // -----------------------------------------------------------------------

    struct GroupData
    {
        uint32_t meshId = 0;
        std::vector<InstanceEntry> instances;
        bool anyVisible = false;
    };

    ankerl::unordered_dense::map<uint32_t, GroupData> groups;

    auto instView = reg.view<InstancedMeshComponent, WorldTransformComponent>();
    instView.each(
        [&](ecs::EntityID entity, const InstancedMeshComponent& imc,
            const WorldTransformComponent& wtc)
        {
            GroupData& gd = groups[imc.instanceGroupId];
            if (gd.instances.empty())
                gd.meshId = imc.mesh;

            const bool visible = reg.has<VisibleTag>(entity);
            gd.anyVisible = gd.anyVisible || visible;
            gd.instances.push_back({wtc.matrix, visible});
        });

    if (groups.empty())
        return;

    // -----------------------------------------------------------------------
    // Determine instancing support once per update call.
    // -----------------------------------------------------------------------
    const bool supportsInstancing = (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) != 0;

    // -----------------------------------------------------------------------
    // Pass 2 — submit one draw call per visible group.
    // -----------------------------------------------------------------------
    bgfx::Encoder* enc = supportsInstancing ? bgfx::begin() : nullptr;

    for (auto& [groupId, gd] : groups)
    {
        // Conservative group cull: skip the whole group if no entity is visible.
        if (!gd.anyVisible)
            continue;

        const Mesh* mesh = res.getMesh(gd.meshId);
        if (!mesh || !mesh->isValid())
            continue;

        if (supportsInstancing)
        {
            submitInstanced(*mesh, gd.instances, enc, program);
        }
        else
        {
            submitFallback(*mesh, gd.instances, program);
        }
    }

    if (enc)
        bgfx::end(enc);
}

}  // namespace engine::rendering
