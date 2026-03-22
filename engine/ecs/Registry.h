#pragma once

#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Entity.h"
#include "SparseSet.h"
#include "View.h"

namespace engine::ecs
{

class Registry
{
public:
    Registry() = default;
    ~Registry() = default;

    // Non-copyable, movable
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = default;
    Registry& operator=(Registry&&) = default;

    // -----------------------------------------------------------------------
    // Entity lifecycle
    // -----------------------------------------------------------------------

    // Create a new entity, recycling a freed index if one is available.
    [[nodiscard]] EntityID createEntity();

    // Destroy an entity: validate, increment generation, recycle index,
    // and remove all associated components.
    void destroyEntity(EntityID entity);

    // Returns true if the entity ID refers to a currently live entity.
    [[nodiscard]] bool isValid(EntityID entity) const noexcept;

    // -----------------------------------------------------------------------
    // Component management
    // -----------------------------------------------------------------------

    // Construct a component of type T in-place and attach it to entity.
    // Returns a reference to the new component.
    template <typename T, typename... Args>
    T& emplace(EntityID entity, Args&&... args)
    {
        assert(isValid(entity) && "emplace called on invalid entity");
        auto& store = getOrCreateStore<T>();
        store.insert(entity, T{std::forward<Args>(args)...});
        return *store.get(entity);
    }

    // Remove component T from entity (no-op if not present).
    template <typename T>
    void remove(EntityID entity)
    {
        auto* store = findStore<T>();
        if (store)
            store->remove(entity);
    }

    // Returns a pointer to the component, or nullptr if entity is invalid
    // or does not have component T.
    template <typename T>
    [[nodiscard]] T* get(EntityID entity) noexcept
    {
        if (!isValid(entity))
            return nullptr;
        auto* store = findStore<T>();
        if (!store)
            return nullptr;
        return store->get(entity);
    }

    template <typename T>
    [[nodiscard]] const T* get(EntityID entity) const noexcept
    {
        if (!isValid(entity))
            return nullptr;
        const auto* store = findStore<T>();
        if (!store)
            return nullptr;
        return store->get(entity);
    }

    // Returns true if entity is valid and has component T.
    template <typename T>
    [[nodiscard]] bool has(EntityID entity) const noexcept
    {
        if (!isValid(entity))
            return false;
        const auto* store = findStore<T>();
        return store && store->contains(entity);
    }

    // Return a View over all entities possessing all of the listed component types.
    // If any component type has never been used, an empty View is returned with no side effects —
    // findStore returns nullptr for unknown types, and View handles null stores as empty.
    template <typename... Components>
    [[nodiscard]] View<Components...> view()
    {
        return View<Components...>(findStore<Components>()...);
    }

private:
    // Retrieve or create the SparseSet for type T.
    template <typename T>
    SparseSet<T>& getOrCreateStore()
    {
        const std::type_index key{typeid(T)};
        auto it = componentStores_.find(key);
        if (it == componentStores_.end())
        {
            auto [inserted, ok] = componentStores_.emplace(key, std::make_unique<SparseSet<T>>());
            (void)ok;
            return static_cast<SparseSet<T>&>(*inserted->second);
        }
        return static_cast<SparseSet<T>&>(*it->second);
    }

    // Returns nullptr if no store for T exists.
    template <typename T>
    SparseSet<T>* findStore() noexcept
    {
        const std::type_index key{typeid(T)};
        auto it = componentStores_.find(key);
        if (it == componentStores_.end())
            return nullptr;
        return static_cast<SparseSet<T>*>(it->second.get());
    }

    template <typename T>
    const SparseSet<T>* findStore() const noexcept
    {
        const std::type_index key{typeid(T)};
        auto it = componentStores_.find(key);
        if (it == componentStores_.end())
            return nullptr;
        return static_cast<const SparseSet<T>*>(it->second.get());
    }

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------

    // Per-index generation counter. Generation starts at 1 (0 is reserved for
    // INVALID_ENTITY). After destroy, generation is incremented so stale IDs
    // become invalid.
    std::vector<uint32_t> generations_;

    // Recycled entity indices waiting to be reused.
    std::vector<uint32_t> freeList_;

    // One SparseSet per component type, keyed by std::type_index.
    std::unordered_map<std::type_index, std::unique_ptr<ISparseSetBase>> componentStores_;
};

}  // namespace engine::ecs
