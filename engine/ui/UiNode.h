#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "engine/math/Types.h"
#include "engine/memory/InlinedVector.h"

namespace engine::ui
{

// Forward declarations
class UiDrawList;
struct UiEvent;

using UiCallback = std::function<void(class UiNode& sender)>;

// Computed screen-space rectangle after layout pass.
struct ComputedRect
{
    math::Vec2 position;  // top-left in logical pixels
    math::Vec2 size;      // width, height
};

// Anchor point relative to parent's rect. (0,0) = top-left, (1,1) = bottom-right.
struct UiAnchor
{
    math::Vec2 min{0.f, 0.f};  // top-left anchor
    math::Vec2 max{0.f, 0.f};  // bottom-right anchor
};

class UiNode
{
public:
    virtual ~UiNode() = default;

    // Identity
    uint32_t id() const noexcept
    {
        return id_;
    }
    const char* name() const noexcept
    {
        return name_.c_str();
    }

    // Tree structure -- UiCanvas owns all nodes via pool.
    // Raw pointers are safe because parent always outlives children.
    UiNode* parent() const noexcept
    {
        return parent_;
    }
    std::span<UiNode* const> children() const noexcept;

    void addChild(UiNode* child);
    void removeChild(uint32_t childId);

    // Layout -- anchor + offset model (similar to Unity RectTransform)
    UiAnchor anchor;
    math::Vec2 offsetMin{0.f, 0.f};  // pixel offset from anchored min corner
    math::Vec2 offsetMax{0.f, 0.f};  // pixel offset from anchored max corner
    math::Vec2 pivot{0.5f, 0.5f};    // rotation/scale pivot, normalized

    // Computed by layout pass -- read-only to game code
    const ComputedRect& rect() const noexcept
    {
        return computedRect_;
    }

    // Visibility and interaction
    bool visible = true;
    bool interactable = true;
    float opacity = 1.0f;

    // Event handling -- returns true if the event was consumed.
    virtual bool onEvent(const UiEvent& event)
    {
        (void)event;
        return false;
    }

protected:
    virtual void onDraw(UiDrawList& drawList) const = 0;
    virtual void onLayoutComputed() {}

private:
    friend class UiCanvas;

    uint32_t id_ = 0;
    std::string name_;
    UiNode* parent_ = nullptr;
    memory::InlinedVector<UiNode*, 4> children_;
    ComputedRect computedRect_{};
};

}  // namespace engine::ui
