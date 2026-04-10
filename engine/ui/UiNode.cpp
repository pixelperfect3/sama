#include "engine/ui/UiNode.h"

#include <algorithm>
#include <cassert>

namespace engine::ui
{

std::span<UiNode* const> UiNode::children() const noexcept
{
    return {children_.data(), children_.size()};
}

void UiNode::addChild(UiNode* child)
{
    assert(child != nullptr);
    assert(child->parent_ == nullptr && "Child already has a parent");
    assert(child != this && "Cannot add self as child");

    child->parent_ = this;
    children_.push_back(child);
}

void UiNode::removeChild(uint32_t childId)
{
    for (size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i]->id_ == childId)
        {
            children_[i]->parent_ = nullptr;
            children_.erase(children_.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}

}  // namespace engine::ui
