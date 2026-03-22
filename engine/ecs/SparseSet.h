#pragma once

#include <algorithm>
#include <cassert>
#include <limits>
#include <span>
#include <vector>

#include "Entity.h"

namespace engine::ecs
{

// Type-erased base for SparseSet so the Registry can remove components without knowing the type.
class ISparseSetBase
{
public:
    virtual ~ISparseSetBase() = default;
    virtual void removeEntity(EntityID entity) = 0;
};

template <typename T>
class SparseSet : public ISparseSetBase
{
public:
    static constexpr uint32_t NULL_INDEX = std::numeric_limits<uint32_t>::max();

    // Insert a component for the given entity.
    // If the entity already has a component, the existing value is overwritten.
    void insert(EntityID entity, T value)
    {
        const uint32_t idx = entityIndex(entity);
        growSparse(idx);

        if (sparse_[idx] != NULL_INDEX)
        {
            // Overwrite existing component
            dense_[sparse_[idx]] = std::move(value);
            return;
        }

        sparse_[idx] = static_cast<uint32_t>(dense_.size());
        dense_.push_back(std::move(value));
        denseEntities_.push_back(entity);
    }

    // Remove the component for the given entity (swap-and-pop).
    void remove(EntityID entity)
    {
        const uint32_t idx = entityIndex(entity);
        if (idx >= sparse_.size() || sparse_[idx] == NULL_INDEX)
            return;

        assert(!dense_.empty() && "SparseSet dense array is empty but sparse entry is set");
        const uint32_t denseIdx = sparse_[idx];
        const uint32_t lastDenseIdx = static_cast<uint32_t>(dense_.size()) - 1u;

        if (denseIdx != lastDenseIdx)
        {
            // Move last element into the removed slot
            dense_[denseIdx] = std::move(dense_[lastDenseIdx]);
            denseEntities_[denseIdx] = denseEntities_[lastDenseIdx];
            // Update sparse for the entity that was moved
            sparse_[entityIndex(denseEntities_[denseIdx])] = denseIdx;
        }

        dense_.pop_back();
        denseEntities_.pop_back();
        sparse_[idx] = NULL_INDEX;
    }

    // ISparseSetBase interface
    void removeEntity(EntityID entity) override
    {
        remove(entity);
    }

    // Returns a pointer to the component, or nullptr if not present.
    [[nodiscard]] T* get(EntityID entity) noexcept
    {
        const uint32_t idx = entityIndex(entity);
        if (idx >= sparse_.size() || sparse_[idx] == NULL_INDEX)
            return nullptr;
        return &dense_[sparse_[idx]];
    }

    [[nodiscard]] const T* get(EntityID entity) const noexcept
    {
        const uint32_t idx = entityIndex(entity);
        if (idx >= sparse_.size() || sparse_[idx] == NULL_INDEX)
            return nullptr;
        return &dense_[sparse_[idx]];
    }

    [[nodiscard]] bool contains(EntityID entity) const noexcept
    {
        const uint32_t idx = entityIndex(entity);
        return idx < sparse_.size() && sparse_[idx] != NULL_INDEX;
    }

    // Direct access to the packed component array.
    [[nodiscard]] std::span<T> components() noexcept
    {
        return std::span<T>(dense_);
    }

    [[nodiscard]] std::span<const T> components() const noexcept
    {
        return std::span<const T>(dense_);
    }

    [[nodiscard]] std::span<const EntityID> entities() const noexcept
    {
        return std::span<const EntityID>(denseEntities_);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return dense_.size();
    }

    void clear() noexcept
    {
        dense_.clear();
        denseEntities_.clear();
        std::fill(sparse_.begin(), sparse_.end(), NULL_INDEX);
    }

private:
    void growSparse(uint32_t idx)
    {
        if (idx >= sparse_.size())
            sparse_.resize(static_cast<std::size_t>(idx) + 1u, NULL_INDEX);
    }

    std::vector<uint32_t> sparse_;         // entity index → dense index
    std::vector<T> dense_;                 // packed component values
    std::vector<EntityID> denseEntities_;  // parallel to dense_ — which entity owns each slot
};

}  // namespace engine::ecs
