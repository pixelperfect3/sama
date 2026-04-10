#include "engine/ui/UiCanvas.h"

#include <algorithm>
#include <cassert>

namespace engine::ui
{

UiCanvas::UiCanvas(uint32_t screenWidth, uint32_t screenHeight)
    : screenW_(screenWidth), screenH_(screenHeight)
{
    // Create an implicit root node (a simple panel-like container).
    // We use a minimal concrete type inline here.
    struct RootNode final : UiNode
    {
    protected:
        void onDraw(UiDrawList& /*drawList*/) const override
        {
            // Root node draws nothing; it is just a container.
        }
    };

    auto rootOwner = std::make_unique<RootNode>();
    root_ = rootOwner.get();
    root_->id_ = nextId_++;
    root_->name_ = "root";
    root_->anchor.min = {0.f, 0.f};
    root_->anchor.max = {1.f, 1.f};
    nodePool_.push_back(std::move(rootOwner));
}

UiCanvas::~UiCanvas() = default;

void UiCanvas::destroyNode(UiNode* node)
{
    if (!node || node == root_)
    {
        return;
    }

    // Remove from parent
    if (node->parent_)
    {
        node->parent_->removeChild(node->id());
    }

    // Recursively destroy children (collect IDs first to avoid iterator
    // invalidation).
    while (!node->children_.empty())
    {
        destroyNode(node->children_[node->children_.size() - 1]);
    }

    // Remove from pool
    auto it = std::find_if(nodePool_.begin(), nodePool_.end(),
                           [node](const std::unique_ptr<UiNode>& p) { return p.get() == node; });

    if (it != nodePool_.end())
    {
        nodePool_.erase(it);
    }

    layoutDirty_ = true;
}

void UiCanvas::update()
{
    if (layoutDirty_)
    {
        ComputedRect screenRect{};
        screenRect.position = {0.f, 0.f};
        screenRect.size = {static_cast<float>(screenW_), static_cast<float>(screenH_)};
        computeLayout(root_, screenRect);
        layoutDirty_ = false;
    }

    drawList_.clear();
    buildDrawList(root_);
}

void UiCanvas::setScreenSize(uint32_t w, uint32_t h)
{
    if (w != screenW_ || h != screenH_)
    {
        screenW_ = w;
        screenH_ = h;
        layoutDirty_ = true;
    }
}

void UiCanvas::computeLayout(UiNode* node, const ComputedRect& parentRect)
{
    if (!node)
    {
        return;
    }

    // Resolve anchor-based layout relative to parent.
    float anchorMinX = parentRect.position.x + node->anchor.min.x * parentRect.size.x;
    float anchorMinY = parentRect.position.y + node->anchor.min.y * parentRect.size.y;
    float anchorMaxX = parentRect.position.x + node->anchor.max.x * parentRect.size.x;
    float anchorMaxY = parentRect.position.y + node->anchor.max.y * parentRect.size.y;

    float left = anchorMinX + node->offsetMin.x;
    float top = anchorMinY + node->offsetMin.y;
    float right = anchorMaxX + node->offsetMax.x;
    float bottom = anchorMaxY + node->offsetMax.y;

    node->computedRect_.position = {left, top};
    node->computedRect_.size = {right - left, bottom - top};

    node->onLayoutComputed();

    // Recurse into children.
    for (auto* child : node->children_)
    {
        computeLayout(child, node->computedRect_);
    }
}

void UiCanvas::buildDrawList(UiNode* node)
{
    if (!node || !node->visible)
    {
        return;
    }

    node->onDraw(drawList_);

    for (auto* child : node->children_)
    {
        buildDrawList(child);
    }
}

}  // namespace engine::ui
