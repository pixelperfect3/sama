#include "Registry.h"

namespace engine::ecs
{

EntityID Registry::createEntity()
{
    uint32_t index = 0;

    if (!freeList_.empty())
    {
        index = freeList_.back();
        freeList_.pop_back();
        // Generation was already incremented on the last destroyEntity call,
        // so it is ready to use as-is.
    }
    else
    {
        index = static_cast<uint32_t>(generations_.size());
        generations_.push_back(1u);  // Generation 1 — generation 0 is reserved for INVALID_ENTITY
    }

    return makeEntityID(index, generations_[index]);
}

void Registry::destroyEntity(EntityID entity)
{
    if (!isValid(entity))
        return;

    const uint32_t index = entityIndex(entity);

    // Remove all components for this entity from every store
    for (auto& [typeKey, store] : componentStores_)
        store->removeEntity(entity);

    // Increment generation to invalidate any outstanding copies of this EntityID.
    // If generation wraps to 0 (after 2^32 destroys), retire the index permanently
    // rather than re-issuing it with a generation indistinguishable from INVALID_ENTITY.
    ++generations_[index];
    if (generations_[index] == 0u)
    {
        // Index retired — do not push back to freeList_
        return;
    }

    // Recycle the index
    freeList_.push_back(index);
}

bool Registry::isValid(EntityID entity) const noexcept
{
    if (entity == INVALID_ENTITY)
        return false;

    const uint32_t index = entityIndex(entity);
    if (index >= generations_.size())
        return false;

    return generations_[index] == entityGeneration(entity);
}

}  // namespace engine::ecs
