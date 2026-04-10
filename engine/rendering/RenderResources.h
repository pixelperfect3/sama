#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// RenderResources — integer-keyed registry for live bgfx GPU handles.
//
// Maps uint32_t IDs to Mesh (and in later phases: textures, materials, shaders).
// IDs are allocated from a free list backed by a simple generation counter so
// that slots are reused after removal.  Callers store the uint32_t ID in their
// MeshComponent.
//
// Thread safety: single-threaded; all mutations happen on the main thread
// before render systems run.
// ---------------------------------------------------------------------------

class RenderResources
{
public:
    RenderResources() = default;
    ~RenderResources() = default;

    // Non-copyable — each Mesh holds live bgfx handles.
    RenderResources(const RenderResources&) = delete;
    RenderResources& operator=(const RenderResources&) = delete;

    RenderResources(RenderResources&&) = default;
    RenderResources& operator=(RenderResources&&) = default;

    // -----------------------------------------------------------------------
    // Mesh registry
    // -----------------------------------------------------------------------

    // Take ownership of a Mesh, return its stable ID.
    // The returned ID is valid until removeMesh() is called with it.
    uint32_t addMesh(Mesh mesh);

    // Returns a pointer to the Mesh with the given ID, or nullptr if the ID is
    // not live.  Pointer is valid until the next add/remove call.
    [[nodiscard]] const Mesh* getMesh(uint32_t id) const;

    // Destroy the Mesh's bgfx handles and free the slot for reuse.
    // No-op if the ID is not live.
    void removeMesh(uint32_t id);

    // Destroy all live GPU handles and reset the registry to empty.
    // Called during renderer shutdown.
    void destroyAll();

    // -----------------------------------------------------------------------
    // White texture — a 1×1 opaque-white texture used as a default fallback
    // when no texture is bound (e.g. SpriteComponent::textureId == 0).
    // The handle is NOT owned by RenderResources; the caller is responsible
    // for its lifetime.  destroyAll() does not destroy it.
    // -----------------------------------------------------------------------

    void setWhiteTexture(bgfx::TextureHandle h)
    {
        whiteTexture_ = h;
    }
    [[nodiscard]] bgfx::TextureHandle whiteTexture() const
    {
        return whiteTexture_;
    }

    // Neutral normal map: 1×1 pixel of (128, 128, 255, 255) = tangent-space (0, 0, 1).
    // Use as fallback for s_normal when no normal map texture is assigned.
    void setNeutralNormalTexture(bgfx::TextureHandle h)
    {
        neutralNormalTexture_ = h;
    }
    [[nodiscard]] bgfx::TextureHandle neutralNormalTexture() const
    {
        return neutralNormalTexture_;
    }

    // 1×1 white cube texture — fallback for unbound IBL cube samplers (slots 6, 7).
    // The handle is NOT owned by RenderResources; caller is responsible for its lifetime.
    void setWhiteCubeTexture(bgfx::TextureHandle h)
    {
        whiteCubeTexture_ = h;
    }
    [[nodiscard]] bgfx::TextureHandle whiteCubeTexture() const
    {
        return whiteCubeTexture_;
    }

    // -----------------------------------------------------------------------
    // Texture registry — stores non-owned bgfx handles (lifetime managed by
    // the GltfAsset that uploaded them).  IDs are 1-based; 0 = no texture.
    // -----------------------------------------------------------------------

    // Register a texture handle and return its stable ID (1-based).
    // Reuses previously-freed slots before growing the vector.
    uint32_t addTexture(bgfx::TextureHandle h);

    // Return the texture with the given ID, or BGFX_INVALID_HANDLE if not found.
    [[nodiscard]] bgfx::TextureHandle getTexture(uint32_t id) const;

    // Mark a previously addTexture()'d slot as free.  Does NOT call
    // bgfx::destroy on the stored handle — the asset manager (or whatever
    // else uploaded the texture) still owns the underlying GPU resource.
    // Subsequent addTexture() calls may reuse the freed slot id.
    // No-op if the id is 0 or already free.
    void removeTexture(uint32_t id);

    // Number of texture slots currently allocated (including freed slots).
    // Valid ids are 1..textureCount() but some may be empty.
    [[nodiscard]] uint32_t textureCount() const
    {
        return static_cast<uint32_t>(textures_.size());
    }

    // -----------------------------------------------------------------------
    // Material registry
    // -----------------------------------------------------------------------

    // Store a Material and return its stable ID (1-based, 0 = invalid).
    // ID is valid until removeMaterial() is called with it.
    uint32_t addMaterial(Material mat);

    // Returns a pointer to the Material with the given ID, or nullptr if the
    // ID is not live.  Pointer is valid until the next add/remove call.
    [[nodiscard]] const Material* getMaterial(uint32_t id) const;

    // Mutable access to a material.  Returns nullptr if the ID is not live.
    [[nodiscard]] Material* getMaterialMut(uint32_t id);

    // Free the material slot for reuse.  No-op if the ID is not live.
    void removeMaterial(uint32_t id);

private:
    // -----------------------------------------------------------------------
    // Slot-based free-list allocation.
    //
    // slots_[i].occupied == true means meshes_[i] holds a live Mesh.
    // freeList_ contains indices of unoccupied slots ready for reuse.
    // IDs are 1-based (0 is reserved as "invalid") so ID == index + 1.
    // -----------------------------------------------------------------------

    struct Slot
    {
        Mesh mesh;
        bool occupied = false;
    };

    bgfx::TextureHandle whiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle neutralNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteCubeTexture_ = BGFX_INVALID_HANDLE;

    // Non-owned texture handles registered via addTexture().  Index 0 = ID 1.
    std::vector<bgfx::TextureHandle> textures_;

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;

    // -----------------------------------------------------------------------
    // Material slots — same free-list pattern as meshes.
    // Materials carry no GPU handles; they are plain value types.
    // -----------------------------------------------------------------------

    struct MaterialSlot
    {
        Material material;
        bool occupied = false;
    };

    std::vector<MaterialSlot> materialSlots_;
    std::vector<uint32_t> materialFreeList_;

    // Convert between external ID (1-based) and internal index (0-based).
    static constexpr uint32_t toIndex(uint32_t id)
    {
        return id - 1u;
    }
    static constexpr uint32_t toId(uint32_t index)
    {
        return index + 1u;
    }
};

}  // namespace engine::rendering
