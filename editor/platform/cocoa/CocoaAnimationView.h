#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::editor
{

// ---------------------------------------------------------------------------
// AnimationViewState -- snapshot pushed from AnimationPanel to the native
// view each frame (or whenever the observed entity/animator changes).
// ---------------------------------------------------------------------------

struct AnimationViewState
{
    std::string assetLabel;  // e.g. "BrainStem.glb"
    std::vector<std::string> clipNames;
    int currentClipIndex = -1;  // -1 = no clip
    float currentTime = 0.0f;
    float duration = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool looping = false;
    bool hasAnimation = false;  // false -> controls are disabled
};

// ---------------------------------------------------------------------------
// CocoaAnimationView -- native AppKit widget for the editor's animation
// timeline panel. Pimpl pattern: no AppKit headers leak into this header.
// ---------------------------------------------------------------------------

class CocoaAnimationView
{
public:
    CocoaAnimationView();
    ~CocoaAnimationView();

    CocoaAnimationView(const CocoaAnimationView&) = delete;
    CocoaAnimationView& operator=(const CocoaAnimationView&) = delete;

    // Returns the native NSView* (as void*) to embed in the bottom tab view.
    void* nativeView() const;

    // Push a fresh state snapshot. Calling this does NOT fire user callbacks.
    void setState(const AnimationViewState& s);

    // Callback types.
    using ClipSelectedCallback = std::function<void(int newClipIndex)>;
    using PlayCallback = std::function<void()>;
    using PauseCallback = std::function<void()>;
    using StopCallback = std::function<void()>;
    using TimeChangedCallback = std::function<void(float newTime)>;
    using SpeedChangedCallback = std::function<void(float newSpeed)>;
    using LoopChangedCallback = std::function<void(bool looping)>;

    void setClipSelectedCallback(ClipSelectedCallback cb);
    void setPlayCallback(PlayCallback cb);
    void setPauseCallback(PauseCallback cb);
    void setStopCallback(StopCallback cb);
    void setTimeChangedCallback(TimeChangedCallback cb);
    void setSpeedChangedCallback(SpeedChangedCallback cb);
    void setLoopChangedCallback(LoopChangedCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
