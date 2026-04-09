#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::editor
{

// ---------------------------------------------------------------------------
// CocoaHierarchyView -- native NSOutlineView-based hierarchy panel.
//
// Pimpl pattern: no AppKit headers leak into this header.
// ---------------------------------------------------------------------------

class CocoaHierarchyView
{
public:
    struct EntityInfo
    {
        uint64_t entityId = 0;
        std::string name;
        std::string tags;  // e.g. "[T][M][Mat]"
    };

    using SelectionCallback = std::function<void(uint64_t entityId)>;
    using NameChangedCallback = std::function<void(uint64_t entityId, const char* newName)>;

    CocoaHierarchyView();
    ~CocoaHierarchyView();

    CocoaHierarchyView(const CocoaHierarchyView&) = delete;
    CocoaHierarchyView& operator=(const CocoaHierarchyView&) = delete;

    // Returns the native NSScrollView* (as void*) to embed in the split view.
    void* nativeView() const;

    // Set the callback for when the user clicks a row.
    void setSelectionCallback(SelectionCallback cb);

    // Set the callback for when the user edits an entity name (double-click to edit).
    void setNameChangedCallback(NameChangedCallback cb);

    // Rebuild the table data from the given entity list.
    // Only call when entities actually changed (dirty flag).
    void setEntities(const std::vector<EntityInfo>& entities);

    // Highlight the given entity as selected (without triggering callback).
    void setSelectedEntity(uint64_t entityId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
