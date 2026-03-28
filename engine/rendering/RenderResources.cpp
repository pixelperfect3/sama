#include "engine/rendering/RenderResources.h"

namespace engine::rendering
{

uint32_t RenderResources::addMesh(Mesh mesh)
{
    uint32_t index{};

    if (!freeList_.empty())
    {
        index = freeList_.back();
        freeList_.pop_back();
        slots_[index].mesh = std::move(mesh);
        slots_[index].occupied = true;
    }
    else
    {
        index = static_cast<uint32_t>(slots_.size());
        slots_.push_back({std::move(mesh), true});
    }

    return toId(index);
}

const Mesh* RenderResources::getMesh(uint32_t id) const
{
    if (id == 0)
        return nullptr;

    const uint32_t index = toIndex(id);
    if (index >= slots_.size())
        return nullptr;

    const Slot& slot = slots_[index];
    if (!slot.occupied)
        return nullptr;

    return &slot.mesh;
}

void RenderResources::removeMesh(uint32_t id)
{
    if (id == 0)
        return;

    const uint32_t index = toIndex(id);
    if (index >= slots_.size())
        return;

    Slot& slot = slots_[index];
    if (!slot.occupied)
        return;

    slot.mesh.destroy();
    slot.occupied = false;
    freeList_.push_back(index);
}

void RenderResources::destroyAll()
{
    for (Slot& slot : slots_)
    {
        if (slot.occupied)
        {
            slot.mesh.destroy();
            slot.occupied = false;
        }
    }
    slots_.clear();
    freeList_.clear();

    // Materials carry no GPU handles — just clear the slots.
    materialSlots_.clear();
    materialFreeList_.clear();
}

// ---------------------------------------------------------------------------
// Texture registry
// ---------------------------------------------------------------------------

uint32_t RenderResources::addTexture(bgfx::TextureHandle h)
{
    textures_.push_back(h);
    return static_cast<uint32_t>(textures_.size());  // 1-based ID
}

bgfx::TextureHandle RenderResources::getTexture(uint32_t id) const
{
    if (id == 0 || id > static_cast<uint32_t>(textures_.size()))
        return BGFX_INVALID_HANDLE;
    return textures_[id - 1];
}

// ---------------------------------------------------------------------------
// Material registry
// ---------------------------------------------------------------------------

uint32_t RenderResources::addMaterial(Material mat)
{
    uint32_t index{};

    if (!materialFreeList_.empty())
    {
        index = materialFreeList_.back();
        materialFreeList_.pop_back();
        materialSlots_[index].material = mat;
        materialSlots_[index].occupied = true;
    }
    else
    {
        index = static_cast<uint32_t>(materialSlots_.size());
        materialSlots_.push_back({mat, true});
    }

    return toId(index);
}

const Material* RenderResources::getMaterial(uint32_t id) const
{
    if (id == 0)
        return nullptr;

    const uint32_t index = toIndex(id);
    if (index >= materialSlots_.size())
        return nullptr;

    const MaterialSlot& slot = materialSlots_[index];
    if (!slot.occupied)
        return nullptr;

    return &slot.material;
}

Material* RenderResources::getMaterialMut(uint32_t id)
{
    if (id == 0)
        return nullptr;

    const uint32_t index = toIndex(id);
    if (index >= materialSlots_.size())
        return nullptr;

    MaterialSlot& slot = materialSlots_[index];
    if (!slot.occupied)
        return nullptr;

    return &slot.material;
}

void RenderResources::removeMaterial(uint32_t id)
{
    if (id == 0)
        return;

    const uint32_t index = toIndex(id);
    if (index >= materialSlots_.size())
        return;

    MaterialSlot& slot = materialSlots_[index];
    if (!slot.occupied)
        return;

    slot.occupied = false;
    materialFreeList_.push_back(index);
}

}  // namespace engine::rendering
