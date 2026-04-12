#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::editor
{

// ---------------------------------------------------------------------------
// CocoaPropertiesView -- native NSStackView-based properties panel.
//
// Pimpl pattern: no AppKit headers leak into this header.
// ---------------------------------------------------------------------------

class CocoaPropertiesView
{
public:
    // A single property field that can be displayed.
    struct PropertyField
    {
        enum class Type : uint8_t
        {
            Label,          // Read-only label
            FloatField,     // Editable float
            ColorField,     // Color display (R,G,B)
            SliderField,    // 0..1 slider
            Header,         // Section header
            DropdownField,  // NSPopUpButton with a list of options + current index
            TextureField,   // Texture slot: shows filename + a Browse button
            CheckboxField   // Boolean toggle
        };

        Type type = Type::Label;
        std::string label;
        float value = 0.0f;
        float minVal = 0.0f;
        float maxVal = 1.0f;
        float color[3] = {1.0f, 1.0f, 1.0f};
        int fieldId = 0;  // Unique ID for callback

        // DropdownField only:
        std::vector<std::string> options;
        int currentIndex = 0;

        // TextureField only: filename of the currently bound texture (empty
        // if none). The Browse button opens an NSOpenPanel filtered to image
        // files; the resulting absolute path is sent through
        // TextureChangedCallback. An empty `texturePath` displays as "(none)".
        std::string texturePath;

        // CheckboxField only:
        bool checked = false;
    };

    using ValueChangedCallback = std::function<void(int fieldId, float newValue)>;
    using ColorChangedCallback = std::function<void(int fieldId, float r, float g, float b)>;
    using IntChangedCallback = std::function<void(int fieldId, int newIndex)>;
    using TextureChangedCallback = std::function<void(int fieldId, const std::string& path)>;
    using TextureClearedCallback = std::function<void(int fieldId)>;
    using BoolChangedCallback = std::function<void(int fieldId, bool newValue)>;
    using AddComponentCallback = std::function<void(const std::string& componentType)>;

    CocoaPropertiesView();
    ~CocoaPropertiesView();

    CocoaPropertiesView(const CocoaPropertiesView&) = delete;
    CocoaPropertiesView& operator=(const CocoaPropertiesView&) = delete;

    // Returns the native NSScrollView* (as void*) to embed in the split view.
    void* nativeView() const;

    // Set callbacks.
    void setValueChangedCallback(ValueChangedCallback cb);
    void setColorChangedCallback(ColorChangedCallback cb);
    void setIntChangedCallback(IntChangedCallback cb);
    void setTextureChangedCallback(TextureChangedCallback cb);
    void setTextureClearedCallback(TextureClearedCallback cb);
    void setBoolChangedCallback(BoolChangedCallback cb);
    // Fires when the user picks a component from the "+ Add Component" menu.
    // The componentType string is one of: "directional_light", "point_light",
    // "mesh", "rigid_body", "box_collider".
    void setAddComponentCallback(AddComponentCallback cb);

    // Rebuild all property fields. Only call when selection changes or values dirty.
    void setProperties(const std::vector<PropertyField>& fields);

    // Update a single float value without rebuilding the entire view.
    void updateFieldValue(int fieldId, float value);

    // Clear all properties (no selection).
    void clear(const char* message = "No entity selected");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
