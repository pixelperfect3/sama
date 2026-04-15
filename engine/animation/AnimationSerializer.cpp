#include "engine/animation/AnimationSerializer.h"

#include <cstdio>
#include <cstring>

#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationResources.h"
#include "engine/io/Json.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// Compare enum <-> string helpers
// ---------------------------------------------------------------------------

static const char* compareToString(TransitionCondition::Compare c)
{
    switch (c)
    {
        case TransitionCondition::Compare::Greater:
            return "greater";
        case TransitionCondition::Compare::Less:
            return "less";
        case TransitionCondition::Compare::Equal:
            return "equal";
        case TransitionCondition::Compare::NotEqual:
            return "notEqual";
        case TransitionCondition::Compare::BoolTrue:
            return "boolTrue";
        case TransitionCondition::Compare::BoolFalse:
            return "boolFalse";
    }
    return "greater";
}

static TransitionCondition::Compare stringToCompare(const char* str)
{
    if (std::strcmp(str, "greater") == 0)
        return TransitionCondition::Compare::Greater;
    if (std::strcmp(str, "less") == 0)
        return TransitionCondition::Compare::Less;
    if (std::strcmp(str, "equal") == 0)
        return TransitionCondition::Compare::Equal;
    if (std::strcmp(str, "notEqual") == 0)
        return TransitionCondition::Compare::NotEqual;
    if (std::strcmp(str, "boolTrue") == 0)
        return TransitionCondition::Compare::BoolTrue;
    if (std::strcmp(str, "boolFalse") == 0)
        return TransitionCondition::Compare::BoolFalse;
    return TransitionCondition::Compare::Greater;
}

// ---------------------------------------------------------------------------
// saveEvents
// ---------------------------------------------------------------------------

bool saveEvents(const AnimationResources& res, const std::string& path)
{
    io::JsonWriter writer(true);
    writer.startObject();
    writer.key("clips");
    writer.startArray();

    for (uint32_t i = 0; i < res.clipCount(); ++i)
    {
        const auto* clip = res.getClip(i);
        if (!clip || clip->events.empty())
            continue;

        writer.startObject();
        writer.key("name");
        writer.writeString(clip->name.c_str());
        writer.key("events");
        writer.startArray();

        for (const auto& evt : clip->events)
        {
            writer.startObject();
            writer.key("name");
            writer.writeString(evt.name.c_str());
            writer.key("time");
            writer.writeFloat(evt.time);
            writer.endObject();
        }

        writer.endArray();
        writer.endObject();
    }

    writer.endArray();
    writer.endObject();

    return writer.writeToFile(path.c_str());
}

// ---------------------------------------------------------------------------
// loadEvents
// ---------------------------------------------------------------------------

bool loadEvents(AnimationResources& res, const std::string& path)
{
    io::JsonDocument doc;
    if (!doc.parseFile(path.c_str()))
    {
        fprintf(stderr, "AnimationSerializer: failed to parse events file: %s\n", path.c_str());
        return false;
    }

    auto root = doc.root();
    if (!root.isObject())
    {
        fprintf(stderr, "AnimationSerializer: events file root is not an object: %s\n",
                path.c_str());
        return false;
    }

    auto clipsArr = root["clips"];
    if (!clipsArr.isArray())
    {
        fprintf(stderr, "AnimationSerializer: missing 'clips' array in: %s\n", path.c_str());
        return false;
    }

    for (size_t ci = 0; ci < clipsArr.arraySize(); ++ci)
    {
        auto clipJson = clipsArr[ci];
        if (!clipJson.isObject())
            continue;

        const char* clipName = clipJson["name"].getString("");
        if (clipName[0] == '\0')
            continue;

        // Find a matching clip by name.
        AnimationClip* targetClip = nullptr;
        for (uint32_t i = 0; i < res.clipCount(); ++i)
        {
            auto* clip = res.getClipMut(i);
            if (clip && clip->name == clipName)
            {
                targetClip = clip;
                break;
            }
        }

        if (!targetClip)
            continue;  // gracefully skip unknown clips

        auto eventsArr = clipJson["events"];
        if (!eventsArr.isArray())
            continue;

        for (size_t ei = 0; ei < eventsArr.arraySize(); ++ei)
        {
            auto evtJson = eventsArr[ei];
            if (!evtJson.isObject())
                continue;

            const char* evtName = evtJson["name"].getString("");
            float evtTime = evtJson["time"].getFloat(0.0f);

            if (evtName[0] != '\0')
            {
                targetClip->addEvent(evtTime, evtName);
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// saveStateMachine
// ---------------------------------------------------------------------------

bool saveStateMachine(const AnimStateMachine& machine, const AnimationResources& res,
                      const std::string& path)
{
    io::JsonWriter writer(true);
    writer.startObject();

    writer.key("defaultState");
    writer.writeUint(machine.defaultState);

    writer.key("states");
    writer.startArray();

    for (const auto& state : machine.states)
    {
        writer.startObject();

        writer.key("name");
        writer.writeString(state.name.c_str());

        // Resolve clip name from resources.
        const auto* clip = res.getClip(state.clipId);
        writer.key("clip");
        writer.writeString(clip ? clip->name.c_str() : "");
        writer.key("speed");
        writer.writeFloat(state.speed);
        writer.key("loop");
        writer.writeBool(state.loop);

        writer.key("transitions");
        writer.startArray();

        for (const auto& tr : state.transitions)
        {
            writer.startObject();

            writer.key("target");
            writer.writeUint(tr.targetState);
            writer.key("blendDuration");
            writer.writeFloat(tr.blendDuration);
            writer.key("exitTime");
            writer.writeFloat(tr.exitTime);
            writer.key("hasExitTime");
            writer.writeBool(tr.hasExitTime);

            writer.key("conditions");
            writer.startArray();

            for (const auto& cond : tr.conditions)
            {
                writer.startObject();
                writer.key("param");
                // Prefer paramName on the condition; fall back to the
                // machine-level paramNames registry.
                const char* pName = cond.paramName.c_str();
                if (cond.paramName.empty())
                {
                    auto it = machine.paramNames.find(cond.paramHash);
                    if (it != machine.paramNames.end())
                        pName = it->second.c_str();
                }
                writer.writeString(pName);
                writer.key("compare");
                writer.writeString(compareToString(cond.compare));
                writer.key("threshold");
                writer.writeFloat(cond.threshold);
                writer.endObject();
            }

            writer.endArray();
            writer.endObject();
        }

        writer.endArray();
        writer.endObject();
    }

    writer.endArray();
    writer.endObject();

    return writer.writeToFile(path.c_str());
}

// ---------------------------------------------------------------------------
// loadStateMachine
// ---------------------------------------------------------------------------

bool loadStateMachine(AnimStateMachine& machine, const AnimationResources& res,
                      const std::string& path)
{
    io::JsonDocument doc;
    if (!doc.parseFile(path.c_str()))
    {
        fprintf(stderr, "AnimationSerializer: failed to parse state machine file: %s\n",
                path.c_str());
        return false;
    }

    auto root = doc.root();
    if (!root.isObject())
    {
        fprintf(stderr, "AnimationSerializer: state machine root is not an object: %s\n",
                path.c_str());
        return false;
    }

    machine.states.clear();
    machine.defaultState = root["defaultState"].getUint(0);

    auto statesArr = root["states"];
    if (!statesArr.isArray())
    {
        fprintf(stderr, "AnimationSerializer: missing 'states' array in: %s\n", path.c_str());
        return false;
    }

    for (size_t si = 0; si < statesArr.arraySize(); ++si)
    {
        auto stateJson = statesArr[si];
        if (!stateJson.isObject())
            continue;

        const char* stateName = stateJson["name"].getString("");
        const char* clipName = stateJson["clip"].getString("");
        float speed = stateJson["speed"].getFloat(1.0f);
        bool loop = stateJson["loop"].getBool(true);

        // Resolve clip ID by name.
        uint32_t clipId = UINT32_MAX;
        for (uint32_t i = 0; i < res.clipCount(); ++i)
        {
            const auto* clip = res.getClip(i);
            if (clip && clip->name == clipName)
            {
                clipId = i;
                break;
            }
        }

        uint32_t stateIdx = machine.addState(stateName, clipId, loop, speed);

        // Parse transitions.
        auto transArr = stateJson["transitions"];
        if (!transArr.isArray())
            continue;

        for (size_t ti = 0; ti < transArr.arraySize(); ++ti)
        {
            auto trJson = transArr[ti];
            if (!trJson.isObject())
                continue;

            StateTransition tr;
            tr.targetState = trJson["target"].getUint(0);
            tr.blendDuration = trJson["blendDuration"].getFloat(0.2f);
            tr.exitTime = trJson["exitTime"].getFloat(0.0f);
            tr.hasExitTime = trJson["hasExitTime"].getBool(false);

            auto condsArr = trJson["conditions"];
            if (condsArr.isArray())
            {
                for (size_t ci = 0; ci < condsArr.arraySize(); ++ci)
                {
                    auto condJson = condsArr[ci];
                    if (!condJson.isObject())
                        continue;

                    TransitionCondition cond;
                    const char* paramStr = condJson["param"].getString("");
                    cond.paramName = paramStr;
                    cond.paramHash = fnv1aHash(paramStr);
                    if (paramStr[0] != '\0')
                    {
                        machine.paramNames[cond.paramHash] = paramStr;
                    }
                    cond.compare = stringToCompare(condJson["compare"].getString("greater"));
                    cond.threshold = condJson["threshold"].getFloat(0.0f);
                    tr.conditions.push_back(std::move(cond));
                }
            }

            machine.states[stateIdx].transitions.push_back(std::move(tr));
        }
    }

    return true;
}

}  // namespace engine::animation
