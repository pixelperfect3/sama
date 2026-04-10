#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/panels/IEditorPanel.h"

// Forward declarations.
namespace engine::ecs
{
class Registry;
}

namespace engine::editor
{

class EditorState;
class IEditorWindow;

// ---------------------------------------------------------------------------
// AssetBrowserPanel -- scans a directory for asset files and lists them
// using bgfx debug text.  Clicking a file name will be used in future
// phases to load assets into the scene.
// ---------------------------------------------------------------------------

class AssetBrowserPanel final : public IEditorPanel
{
public:
    AssetBrowserPanel(ecs::Registry& registry, EditorState& state, const IEditorWindow& window);
    ~AssetBrowserPanel() override = default;

    const char* panelName() const override
    {
        return "Asset Browser";
    }

    void init() override;
    void shutdown() override;
    void update(float dt) override;
    void render() override;

    // Set the directory to scan for assets.
    void setAssetDirectory(const char* path);

    // Rescan the asset directory.
    void refresh();

private:
    ecs::Registry& registry_;
    EditorState& state_;
    const IEditorWindow& window_;

    // One row in the browser list. Computed once in refresh() so render()
    // doesn't have to re-parse extensions every frame.
    struct AssetEntry
    {
        std::string filename;
        const char* icon = "[?]";  // pointer to a static literal — no alloc
    };

    std::string assetDir_;
    std::vector<AssetEntry> assetFiles_;

    // Scroll offset for the file list.
    int scrollOffset_ = 0;

    // Layout constants (in debug-text character cells).
    static constexpr uint16_t kStartX = 1;
    static constexpr uint16_t kStartY = 4;
    static constexpr uint16_t kMaxRows = 20;
    static constexpr uint16_t kPanelWidth = 40;
};

}  // namespace engine::editor
