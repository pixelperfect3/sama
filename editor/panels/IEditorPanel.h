#pragma once

namespace engine::editor
{

// ---------------------------------------------------------------------------
// IEditorPanel -- interface for editor panels (hierarchy, properties, etc.).
//
// Panels are platform-independent; they render using bgfx debug text for now
// and will be migrated to native UI controls in a later phase.
// ---------------------------------------------------------------------------

class IEditorPanel
{
public:
    virtual ~IEditorPanel() = default;

    virtual const char* panelName() const = 0;

    virtual void init() = 0;
    virtual void shutdown() = 0;

    // Called once per frame to process input and update logic.
    virtual void update(float dt) = 0;

    // Called once per frame to render panel contents (bgfx debug text).
    virtual void render() = 0;

    // Visibility toggle.
    void setVisible(bool v)
    {
        visible_ = v;
    }
    [[nodiscard]] bool isVisible() const
    {
        return visible_;
    }

private:
    bool visible_ = true;
};

}  // namespace engine::editor
