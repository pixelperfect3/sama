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

    // Full state machine editing data.
    struct StateInfo
    {
        std::string name;
        std::string clipName;
        float speed = 1.0f;
        bool loop = true;
        struct TransitionInfo
        {
            int targetState = 0;
            std::string targetName;
            float blendDuration = 0.3f;
            float exitTime = 0.0f;
            bool hasExitTime = false;
            struct ConditionInfo
            {
                std::string paramName;
                int compare = 0;  // index into Compare enum
                float threshold = 0.0f;
            };
            std::vector<ConditionInfo> conditions;
        };
        std::vector<TransitionInfo> transitions;
    };
    std::vector<StateInfo> stateInfos;
    int selectedStateIndex = -1;
    int selectedTransitionIndex = -1;
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
    using StateAddedCallback = std::function<void()>;
    using StateRemovedCallback = std::function<void(int stateIndex)>;
    using StateEditedCallback = std::function<void(int stateIndex, const std::string& name,
                                                   int clipIndex, float speed, bool loop)>;
    using TransitionAddedCallback =
        std::function<void(int fromState, int toState, float blendDuration)>;
    using TransitionRemovedCallback = std::function<void(int fromState, int transitionIndex)>;
    using TransitionEditedCallback =
        std::function<void(int fromState, int transitionIndex, int targetState, float blendDuration,
                           float exitTime, bool hasExitTime)>;
    using ConditionAddedCallback =
        std::function<void(int fromState, int transitionIndex, const std::string& param,
                           int compare, float threshold)>;
    using ConditionRemovedCallback =
        std::function<void(int fromState, int transitionIndex, int conditionIndex)>;
    using ParamAddedCallback = std::function<void(const std::string& name, bool isBool)>;
    using StateSelectedCallback = std::function<void(int stateIndex)>;
    using TransitionSelectedCallback = std::function<void(int stateIndex, int transitionIndex)>;

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
    void setStateAddedCallback(StateAddedCallback cb);
    void setStateRemovedCallback(StateRemovedCallback cb);
    void setStateEditedCallback(StateEditedCallback cb);
    void setTransitionAddedCallback(TransitionAddedCallback cb);
    void setTransitionRemovedCallback(TransitionRemovedCallback cb);
    void setTransitionEditedCallback(TransitionEditedCallback cb);
    void setConditionAddedCallback(ConditionAddedCallback cb);
    void setConditionRemovedCallback(ConditionRemovedCallback cb);
    void setParamAddedCallback(ParamAddedCallback cb);
    void setStateSelectedCallback(StateSelectedCallback cb);
    void setTransitionSelectedCallback(TransitionSelectedCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
