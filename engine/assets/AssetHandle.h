#pragma once

#include <cstdint>

namespace engine::assets
{

// ---------------------------------------------------------------------------
// AssetHandle<T> — typed (index, generation) reference to a loaded asset.
//
// index      — 1-based slot in the AssetRegistry dense array (0 = invalid).
// generation — bumped each time the slot is reused; stale handles silently
//              return nullptr from AssetManager::get() rather than crashing.
//
// T is a phantom type that prevents mixing handle types at compile time.
// AssetHandle<Mesh> and AssetHandle<Texture> are distinct, incompatible
// types even though both compile to the same 8-byte representation.
//
// Handles are cheap to copy and safe to hold across frames.
// ---------------------------------------------------------------------------

template <typename T>
struct AssetHandle
{
    uint32_t index = 0;  // 0 = invalid sentinel
    uint32_t generation = 0;

    [[nodiscard]] bool isValid() const noexcept
    {
        return index != 0;
    }

    bool operator==(const AssetHandle&) const = default;
    bool operator!=(const AssetHandle&) const = default;
};

}  // namespace engine::assets
