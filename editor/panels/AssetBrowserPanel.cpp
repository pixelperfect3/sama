#include "editor/panels/AssetBrowserPanel.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "editor/EditorState.h"
#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Registry.h"

namespace fs = std::filesystem;

namespace engine::editor
{

AssetBrowserPanel::AssetBrowserPanel(ecs::Registry& registry, EditorState& state,
                                     const IEditorWindow& window)
    : registry_(registry), state_(state), window_(window)
{
}

void AssetBrowserPanel::init()
{
    // Default to current directory assets.
    if (assetDir_.empty())
    {
        assetDir_ = ".";
    }
    refresh();
}

void AssetBrowserPanel::shutdown() {}

void AssetBrowserPanel::setAssetDirectory(const char* path)
{
    assetDir_ = path;
    refresh();
}

void AssetBrowserPanel::refresh()
{
    assetFiles_.clear();

    if (!fs::exists(assetDir_))
        return;

    for (const auto& entry : fs::directory_iterator(assetDir_))
    {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        // Convert to lowercase for comparison.
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (ext == ".glb" || ext == ".gltf" || ext == ".obj" || ext == ".png" || ext == ".jpg" ||
            ext == ".jpeg" || ext == ".hdr" || ext == ".wav" || ext == ".ogg" || ext == ".mp3")
        {
            assetFiles_.push_back(entry.path().filename().string());
        }
    }

    // Sort alphabetically.
    std::sort(assetFiles_.begin(), assetFiles_.end());
    scrollOffset_ = 0;
}

void AssetBrowserPanel::update(float /*dt*/)
{
    if (!isVisible())
        return;

    // Scroll with up/down arrows when asset browser is focused.
    // Use Shift+Up/Down to scroll the asset browser.
    if (window_.isShiftDown())
    {
        if (window_.isKeyPressed(0x83))  // Up arrow
        {
            scrollOffset_ = std::max(0, scrollOffset_ - 1);
        }
        if (window_.isKeyPressed(0x82))  // Down arrow
        {
            int maxScroll =
                std::max(0, static_cast<int>(assetFiles_.size()) - static_cast<int>(kMaxRows));
            scrollOffset_ = std::min(maxScroll, scrollOffset_ + 1);
        }
    }
}

void AssetBrowserPanel::render()
{
    if (!isVisible())
        return;

    // Render in the lower-left area, below the hierarchy panel.
    // Use row offset to avoid overlapping with hierarchy.
    constexpr uint16_t kAssetStartY = 46;

    // Header.
    bgfx::dbgTextPrintf(kStartX, kAssetStartY - 1, 0x0f, "--- Asset Browser ---");
    bgfx::dbgTextPrintf(kStartX, kAssetStartY, 0x08, "Dir: %s", assetDir_.c_str());

    if (assetFiles_.empty())
    {
        bgfx::dbgTextPrintf(kStartX, kAssetStartY + 1, 0x07, "(no assets found)");
        return;
    }

    uint16_t row = kAssetStartY + 1;
    int endIdx =
        std::min(static_cast<int>(assetFiles_.size()), scrollOffset_ + static_cast<int>(kMaxRows));

    for (int i = scrollOffset_; i < endIdx; ++i)
    {
        const auto& file = assetFiles_[i];

        // Determine file type icon based on extension.
        const char* icon = "[?]";
        auto ext = fs::path(file).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (ext == ".glb" || ext == ".gltf" || ext == ".obj")
        {
            icon = "[3D]";
        }
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr")
        {
            icon = "[Tx]";
        }
        else if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
        {
            icon = "[Au]";
        }

        bgfx::dbgTextPrintf(kStartX, row, 0x07, " %s %-30s", icon, file.c_str());
        ++row;
    }

    // Scroll indicator.
    if (static_cast<int>(assetFiles_.size()) > static_cast<int>(kMaxRows))
    {
        bgfx::dbgTextPrintf(kStartX, row, 0x08, " Shift+Up/Down to scroll (%d/%zu)", scrollOffset_,
                            assetFiles_.size());
    }
}

}  // namespace engine::editor
