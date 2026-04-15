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

struct EventMarker
{
    std::string name;
    float time = 0.0f;
};

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

    std::vector<EventMarker> events;       // all events in current clip
    std::vector<std::string> firedEvents;  // events that fired this frame (for flash)

    // State machine data (empty if entity has no AnimStateMachineComponent).
    std::string currentStateName;
    std::vector<std::string> stateNames;  // all states in the machine
    int currentStateIndex = -1;
    struct ParamInfo
    {
        std::string name;
        float value = 0.0f;
        bool isBool = false;  // if true, value is 0.0 or 1.0
    };
    std::vector<ParamInfo> params;
    bool hasStateMachine = false;
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
    using EventAddedCallback = std::function<void(float time, const std::string& name)>;
    using EventRemovedCallback = std::function<void(int eventIndex)>;
    using EventEditedCallback =
        std::function<void(int eventIndex, float newTime, const std::string& newName)>;
    using StateForceSetCallback = std::function<void(int stateIndex)>;
    using ParamChangedCallback = std::function<void(const std::string& paramName, float value)>;

    void setClipSelectedCallback(ClipSelectedCallback cb);
    void setPlayCallback(PlayCallback cb);
    void setPauseCallback(PauseCallback cb);
    void setStopCallback(StopCallback cb);
    void setTimeChangedCallback(TimeChangedCallback cb);
    void setSpeedChangedCallback(SpeedChangedCallback cb);
    void setLoopChangedCallback(LoopChangedCallback cb);
    void setEventAddedCallback(EventAddedCallback cb);
    void setEventRemovedCallback(EventRemovedCallback cb);
    void setEventEditedCallback(EventEditedCallback cb);
    void setStateForceSetCallback(StateForceSetCallback cb);
    void setParamChangedCallback(ParamChangedCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
