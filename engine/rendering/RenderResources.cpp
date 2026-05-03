#include "engine/rendering/RenderResources.h"

#include <bgfx/bgfx.h>

namespace engine::rendering
{

namespace
{
// Boundary helpers — RenderResources stores TextureHandle (the bgfx-free
// engine wrapper); the bgfx APIs we drive inside this TU still want the real
// bgfx::TextureHandle.  Layout is asserted bit-identical in RenderPass.cpp
// so each conversion is a no-op reinterpret of the underlying uint16_t.
inline bgfx::TextureHandle toBgfx(TextureHandle h)
{
    return bgfx::TextureHandle{h.idx};
}

inline TextureHandle fromBgfx(bgfx::TextureHandle h)
{
    return TextureHandle{h.idx};
}
}  // namespace

// ---------------------------------------------------------------------------
// Default textures
// ---------------------------------------------------------------------------

void RenderResources::createDefaultTextures()
{
    // White 2D — fallback for albedo / ORM slots
    if (!isValid(whiteTexture_))
    {
        const uint8_t kWhite[4] = {255, 255, 255, 255};
        whiteTexture_ =
            fromBgfx(bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                           BGFX_TEXTURE_NONE, bgfx::copy(kWhite, sizeof(kWhite))));
    }

    // Neutral normal — tangent-space (0, 0, 1)
    if (!isValid(neutralNormalTexture_))
    {
        const uint8_t kNeutralNormal[4] = {128, 128, 255, 255};
        neutralNormalTexture_ = fromBgfx(
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kNeutralNormal, sizeof(kNeutralNormal))));
    }

    // White cube — fallback for unbound IBL cube samplers
    if (!isValid(whiteCubeTexture_))
    {
        uint8_t cubeFaces[6 * 4];
        for (int i = 0; i < 6 * 4; ++i)
            cubeFaces[i] = 255;
        whiteCubeTexture_ = fromBgfx(
            bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                    bgfx::copy(cubeFaces, sizeof(cubeFaces))));
    }
}

void RenderResources::destroyDefaultTextures()
{
    if (isValid(whiteTexture_))
    {
        bgfx::destroy(toBgfx(whiteTexture_));
        whiteTexture_ = kInvalidTexture;
    }
    if (isValid(neutralNormalTexture_))
    {
        bgfx::destroy(toBgfx(neutralNormalTexture_));
        neutralNormalTexture_ = kInvalidTexture;
    }
    if (isValid(whiteCubeTexture_))
    {
        bgfx::destroy(toBgfx(whiteCubeTexture_));
        whiteCubeTexture_ = kInvalidTexture;
    }
}

// ---------------------------------------------------------------------------
// Mesh registry
// ---------------------------------------------------------------------------

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

    // Texture handles are non-owned — just drop our records.
    textures_.clear();
    textureFreeList_.clear();

    // Destroy default fallback textures (owned by RenderResources).
    destroyDefaultTextures();
}

// ---------------------------------------------------------------------------
// Texture registry
// ---------------------------------------------------------------------------

uint32_t RenderResources::addTexture(TextureHandle h)
{
    // Reuse a previously freed slot before growing.  The editor frequently
    // adds/removes textures as the user swaps material images; keeping ids
    // bounded avoids unbounded growth across a long session.
    if (!textureFreeList_.empty())
    {
        uint32_t idx = textureFreeList_.back();
        textureFreeList_.pop_back();
        textures_[idx] = h;
        return idx + 1;  // 1-based ID
    }

    textures_.push_back(h);
    return static_cast<uint32_t>(textures_.size());  // 1-based ID
}

TextureHandle RenderResources::getTexture(uint32_t id) const
{
    if (id == 0 || id > static_cast<uint32_t>(textures_.size()))
        return kInvalidTexture;
    return textures_[id - 1];
}

void RenderResources::removeTexture(uint32_t id)
{
    if (id == 0 || id > static_cast<uint32_t>(textures_.size()))
        return;
    textures_[id - 1] = kInvalidTexture;
    textureFreeList_.push_back(id - 1);
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
