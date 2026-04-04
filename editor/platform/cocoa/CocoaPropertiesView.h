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
            Label,        // Read-only label
            FloatField,   // Editable float
            ColorField,   // Color display (R,G,B)
            SliderField,  // 0..1 slider
            Header,       // Section header
        };

        Type type = Type::Label;
        std::string label;
        float value = 0.0f;
        float minVal = 0.0f;
        float maxVal = 1.0f;
        float color[3] = {1.0f, 1.0f, 1.0f};
        int fieldId = 0;  // Unique ID for callback
    };

    using ValueChangedCallback = std::function<void(int fieldId, float newValue)>;
    using ColorChangedCallback = std::function<void(int fieldId, float r, float g, float b)>;

    CocoaPropertiesView();
    ~CocoaPropertiesView();

    CocoaPropertiesView(const CocoaPropertiesView&) = delete;
    CocoaPropertiesView& operator=(const CocoaPropertiesView&) = delete;

    // Returns the native NSScrollView* (as void*) to embed in the split view.
    void* nativeView() const;

    // Set callbacks.
    void setValueChangedCallback(ValueChangedCallback cb);
    void setColorChangedCallback(ColorChangedCallback cb);

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
