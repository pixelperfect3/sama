#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiEvent.h"
#include "engine/ui/UiNode.h"

namespace engine::ui
{

class UiCanvas
{
public:
    UiCanvas(uint32_t screenWidth, uint32_t screenHeight);
    ~UiCanvas();

    // Create a node -- allocated from internal pool.
    template <typename T, typename... Args>
    T* createNode(const char* name, Args&&... args);

    void destroyNode(UiNode* node);

    UiNode* root() noexcept
    {
        return root_;
    }

    // Per-frame: recompute layout if dirty, then walk tree to build draw list.
    void update();

    const UiDrawList& drawList() const noexcept
    {
        return drawList_;
    }

    void setScreenSize(uint32_t w, uint32_t h);

    // Dispatch an event through the tree. Returns true if any node consumed it.
    bool dispatchEvent(const UiEvent& event);

private:
    bool dispatchEventToNode(UiNode* node, const UiEvent& event);
    void computeLayout(UiNode* node, const ComputedRect& parentRect);
    void buildDrawList(UiNode* node);

    UiNode* root_ = nullptr;
    UiDrawList drawList_;
    uint32_t screenW_ = 0;
    uint32_t screenH_ = 0;
    bool layoutDirty_ = true;

    // Simple ownership via unique_ptr vector (optimize to pool later).
    std::vector<std::unique_ptr<UiNode>> nodePool_;
    uint32_t nextId_ = 1;
};

// ---------------------------------------------------------------------------
// Template implementation
// ---------------------------------------------------------------------------

template <typename T, typename... Args>
T* UiCanvas::createNode(const char* name, Args&&... args)
{
    static_assert(std::is_base_of_v<UiNode, T>, "T must derive from UiNode");

    auto node = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = node.get();

    // Set internal fields via friendship.
    raw->id_ = nextId_++;
    raw->name_ = name ? name : "";

    nodePool_.push_back(std::move(node));
    layoutDirty_ = true;
    return raw;
}

}  // namespace engine::ui
