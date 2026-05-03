#include "engine/input/GestureRecognizer.h"

#include <cmath>

#include "engine/input/InputState.h"

namespace engine::input
{

namespace
{

// Pick the first two touch points by stable ID order so the choice is
// deterministic regardless of the order the platform delivers events in.  We
// skip touches in the Ended phase: those are about to disappear and would
// just inject a spike on the next frame when they vanish.
//
// Returns true iff two qualifying touches were found.  The chosen pair is
// returned through the out parameters.
bool pickTrackedPair(const InputState& input, const TouchPoint*& outFirst,
                     const TouchPoint*& outSecond)
{
    outFirst = nullptr;
    outSecond = nullptr;

    const TouchPoint* a = nullptr;
    const TouchPoint* b = nullptr;
    for (const auto& t : input.touches())
    {
        if (t.phase == TouchPoint::Phase::Ended)
            continue;

        if (!a)
        {
            a = &t;
            continue;
        }
        if (!b)
        {
            b = &t;
            continue;
        }
        // Maintain (a,b) as the lowest two ids so the pair we track is
        // stable across frames even if a third finger comes down.
        if (t.id < a->id)
        {
            b = a;
            a = &t;
        }
        else if (t.id < b->id)
        {
            b = &t;
        }
    }

    if (!a || !b)
        return false;

    // Order the pair by id so trackedId0_ < trackedId1_ — this gives us a
    // canonical pair representation for the change-detection compare below.
    if (a->id > b->id)
    {
        const TouchPoint* tmp = a;
        a = b;
        b = tmp;
    }
    outFirst = a;
    outSecond = b;
    return true;
}

}  // namespace

void GestureRecognizer::reset()
{
    tracking_ = false;
    trackedId0_ = 0;
    trackedId1_ = 0;
    lastDistance_ = 0.f;
    lastMidX_ = 0.f;
    lastMidY_ = 0.f;
}

void GestureRecognizer::update(const InputState& input, GestureState& outState)
{
    outState.pinchDelta = 0.f;
    outState.panDeltaX = 0.f;
    outState.panDeltaY = 0.f;
    outState.active = false;

    const TouchPoint* a = nullptr;
    const TouchPoint* b = nullptr;
    if (!pickTrackedPair(input, a, b))
    {
        // Fewer than two active touches — drop tracking entirely.  Next
        // multi-touch frame will start a fresh gesture.
        tracking_ = false;
        return;
    }

    const float dx = b->x - a->x;
    const float dy = b->y - a->y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float midX = 0.5f * (a->x + b->x);
    const float midY = 0.5f * (a->y + b->y);

    outState.active = true;

    const bool sameTracked = tracking_ && trackedId0_ == a->id && trackedId1_ == b->id;

    if (!sameTracked)
    {
        // Either: starting tracking for the first time, or one of the
        // tracked fingers lifted and a new one took its place.  Re-anchor
        // on the new pair without emitting a delta — the position
        // discontinuity between the old and new pair would otherwise show
        // up as a one-frame spike on every gesture restart.
        tracking_ = true;
        trackedId0_ = a->id;
        trackedId1_ = b->id;
        lastDistance_ = dist;
        lastMidX_ = midX;
        lastMidY_ = midY;
        return;
    }

    outState.pinchDelta = dist - lastDistance_;
    outState.panDeltaX = midX - lastMidX_;
    outState.panDeltaY = midY - lastMidY_;

    lastDistance_ = dist;
    lastMidX_ = midX;
    lastMidY_ = midY;
}

}  // namespace engine::input
