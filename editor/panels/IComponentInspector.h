#pragma once

#include <cstdint>

// Forward declarations.
namespace engine::ecs
{
class Registry;
using EntityID = uint64_t;
}  // namespace engine::ecs

namespace engine::editor
{

class EditorState;

// ---------------------------------------------------------------------------
// IComponentInspector -- interface for per-component-type property inspectors.
//
// Each component type (Transform, Material, Light, etc.) implements this
// interface to provide inspection and editing UI.
//
// `EditorState&` is threaded through `inspect()` so inspectors that mutate
// runtime-owned data (TransformComponent, RigidBodyComponent, ...) can
// honour the play-state gate. Read-only inspectors may ignore it.
// ---------------------------------------------------------------------------

class IComponentInspector
{
public:
    virtual ~IComponentInspector() = default;

    // Human-readable component name (e.g., "Transform", "Material").
    virtual const char* componentName() const = 0;

    // Returns true if the entity has this component.
    virtual bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const = 0;

    // Render the inspector UI for this component (bgfx debug text for now).
    // startRow: the debug text row to start rendering at.
    // state: editor state — inspectors gate write-back on `playState()`.
    // Returns the number of rows consumed.
    virtual uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, const EditorState& state,
                             uint16_t startRow) = 0;
};

}  // namespace engine::editor
