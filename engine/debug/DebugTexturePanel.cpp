#include "engine/debug/DebugTexturePanel.h"

#include <dear-imgui/imgui.h>

#include <cstdint>
#include <string>

namespace engine::debug
{

// ---------------------------------------------------------------------------
// Encode a bgfx::TextureHandle as an ImTextureID the bgfx imgui backend can
// decode.  This mirrors ImGui::toId() defined in bgfx's imgui.h wrapper, but
// avoids a header dependency on the examples directory.
//
// Layout (matching the bgfx imgui backend's decode logic):
//   bits [0..15]  — bgfx::TextureHandle::idx
//   bits [16..23] — flags  (0x01 = IMGUI_FLAGS_ALPHA_BLEND)
//   bits [24..31] — mip    (0 = base mip)
// ---------------------------------------------------------------------------

namespace
{

ImTextureID toImTexId(bgfx::TextureHandle handle, uint8_t flags = 0x01, uint8_t mip = 0)
{
    union
    {
        struct
        {
            bgfx::TextureHandle handle;
            uint8_t flags;
            uint8_t mip;
        } s;
        ImTextureID id;
    } tex{};
    tex.s.handle = handle;
    tex.s.flags = flags;
    tex.s.mip = mip;
    return tex.id;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------

void DebugTexturePanel::add(bgfx::TextureHandle handle, std::string label)
{
    m_textures.push_back({handle, std::move(label)});
}

void DebugTexturePanel::clear()
{
    m_textures.clear();
}

bool DebugTexturePanel::show(float thumbSize)
{
    // First-use defaults: right side of a 1280-wide window, below the menu bar.
    ImGui::SetNextWindowPos(ImVec2(980.f, 20.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(thumbSize + 48.f, 660.f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Textures");

    ImGui::Text("%d texture(s) loaded", static_cast<int>(m_textures.size()));

    for (const TextureEntry& e : m_textures)
    {
        // Each texture gets a collapsing header — click to expand/collapse.
        if (ImGui::CollapsingHeader(e.label.c_str()))
        {
            if (bgfx::isValid(e.handle))
            {
                ImGui::Image(toImTexId(e.handle), ImVec2(thumbSize, thumbSize), ImVec2(0.f, 0.f),
                             ImVec2(1.f, 1.f));
            }
            else
            {
                ImGui::TextDisabled("(invalid handle)");
            }
        }
    }

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    ImGui::End();
    return hovered;
}

}  // namespace engine::debug
