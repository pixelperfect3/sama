#pragma once

#include <cstddef>
#include <limits>
#include <span>
#include <tuple>

#include "Entity.h"
#include "SparseSet.h"

namespace engine::ecs
{

// ---------------------------------------------------------------------------
// ViewIterator — yields std::tuple<EntityID, Components&...>
// Advances past any entity that does not have ALL required components.
// ---------------------------------------------------------------------------

template <typename... Components>
class ViewIterator
{
public:
    using value_type = std::tuple<EntityID, Components&...>;
    using Stores = std::tuple<SparseSet<Components>*...>;

    ViewIterator(Stores stores, const EntityID* ptr, const EntityID* end) noexcept
        : stores_(stores), ptr_(ptr), end_(end)
    {
        skipInvalid();
    }

    [[nodiscard]] value_type operator*() const
    {
        return deref(std::index_sequence_for<Components...>{});
    }

    ViewIterator& operator++() noexcept
    {
        ++ptr_;
        skipInvalid();
        return *this;
    }

    [[nodiscard]] bool operator==(const ViewIterator& o) const noexcept
    {
        return ptr_ == o.ptr_;
    }
    [[nodiscard]] bool operator!=(const ViewIterator& o) const noexcept
    {
        return ptr_ != o.ptr_;
    }

private:
    void skipInvalid() noexcept
    {
        while (ptr_ != end_ && !hasAll(*ptr_))
            ++ptr_;
    }

    [[nodiscard]] bool hasAll(EntityID e) const noexcept
    {
        return hasAllImpl(e, std::index_sequence_for<Components...>{});
    }

    template <std::size_t... Is>
    [[nodiscard]] bool hasAllImpl(EntityID e, std::index_sequence<Is...>) const noexcept
    {
        return ((std::get<Is>(stores_) && std::get<Is>(stores_)->contains(e)) && ...);
    }

    template <std::size_t... Is>
    [[nodiscard]] value_type deref(std::index_sequence<Is...>) const
    {
        return value_type{*ptr_, *std::get<Is>(stores_)->get(*ptr_)...};
    }

    Stores stores_;
    const EntityID* ptr_;
    const EntityID* end_;
};

// ---------------------------------------------------------------------------
// View<Components...>
// Holds non-owning pointers to one SparseSet per component type.
// Iteration selects the smallest set as the outer loop.
// ---------------------------------------------------------------------------

template <typename... Components>
class View
{
    static_assert(sizeof...(Components) > 0, "View requires at least one component type");

    using Stores = std::tuple<SparseSet<Components>*...>;

public:
    explicit View(SparseSet<Components>*... stores) noexcept : stores_(stores...) {}

    // Call func(EntityID, Components&...) for every entity that has all components.
    template <typename Func>
    void each(Func&& func)
    {
        eachImpl(std::forward<Func>(func), std::index_sequence_for<Components...>{});
    }

    // Range-based for support.
    [[nodiscard]] auto begin()
    {
        return makeIter(false);
    }
    [[nodiscard]] auto end()
    {
        return makeIter(true);
    }

private:
    // Returns {begin ptr, end ptr} of the smallest SparseSet's entity array.
    // A null store pointer (component type never used) is treated as size 0 — the view
    // will iterate zero entities, with no stores created as a side effect.
    std::pair<const EntityID*, const EntityID*> smallestSpan()
    {
        const EntityID* b = nullptr;
        const EntityID* e = nullptr;
        std::size_t minSz = std::numeric_limits<std::size_t>::max();

        auto check = [&](auto* store)
        {
            const std::size_t sz = store ? store->size() : 0u;
            if (sz < minSz)
            {
                minSz = sz;
                if (store)
                {
                    auto sp = store->entities();
                    b = sp.data();
                    e = sp.data() + sp.size();
                }
                else
                {
                    b = nullptr;
                    e = nullptr;
                }
            }
        };
        std::apply([&](auto*... s) { (check(s), ...); }, stores_);
        return {b, e};
    }

    template <typename Func, std::size_t... Is>
    void eachImpl(Func&& func, std::index_sequence<Is...>)
    {
        auto [b, e] = smallestSpan();
        for (const EntityID* p = b; p != e; ++p)
        {
            EntityID eid = *p;
            if ((std::get<Is>(stores_)->contains(eid) && ...))
                func(eid, *std::get<Is>(stores_)->get(eid)...);
        }
    }

    [[nodiscard]] ViewIterator<Components...> makeIter(bool atEnd)
    {
        auto [b, e] = smallestSpan();
        return ViewIterator<Components...>(stores_, atEnd ? e : b, e);
    }

    Stores stores_;
};

}  // namespace engine::ecs
