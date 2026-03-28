#include "engine/physics/JoltLayerConfig.h"

namespace engine::physics
{

// ---------------------------------------------------------------------------
// BroadPhaseLayerInterfaceImpl
// ---------------------------------------------------------------------------

BroadPhaseLayerInterfaceImpl::BroadPhaseLayerInterfaceImpl()
{
    objectToBroadPhase_[Layers::STATIC] = BroadPhaseLayers::NON_MOVING;
    objectToBroadPhase_[Layers::DYNAMIC] = BroadPhaseLayers::MOVING;
    objectToBroadPhase_[Layers::KINEMATIC] = BroadPhaseLayers::MOVING;
    objectToBroadPhase_[Layers::TRIGGER] = BroadPhaseLayers::MOVING;
    objectToBroadPhase_[Layers::DEBRIS] = BroadPhaseLayers::MOVING;
}

JPH::uint BroadPhaseLayerInterfaceImpl::GetNumBroadPhaseLayers() const
{
    return BroadPhaseLayers::NUM_LAYERS;
}

JPH::BroadPhaseLayer BroadPhaseLayerInterfaceImpl::GetBroadPhaseLayer(
    JPH::ObjectLayer inLayer) const
{
    JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
    return objectToBroadPhase_[inLayer];
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* BroadPhaseLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const
{
    switch ((JPH::BroadPhaseLayer::Type)inLayer)
    {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
            return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
            return "MOVING";
        default:
            JPH_ASSERT(false);
            return "UNKNOWN";
    }
}
#endif

// ---------------------------------------------------------------------------
// ObjectVsBroadPhaseLayerFilterImpl
// ---------------------------------------------------------------------------

bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1,
                                                      JPH::BroadPhaseLayer inLayer2) const
{
    switch (inLayer1)
    {
        case Layers::STATIC:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::DYNAMIC:
            return true;  // dynamic collides with everything
        case Layers::KINEMATIC:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::TRIGGER:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::DEBRIS:
            return inLayer2 == BroadPhaseLayers::NON_MOVING;
        default:
            JPH_ASSERT(false);
            return false;
    }
}

// ---------------------------------------------------------------------------
// ObjectLayerPairFilterImpl
// ---------------------------------------------------------------------------

bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inObject1,
                                              JPH::ObjectLayer inObject2) const
{
    // Collision table from architecture doc:
    // Static(0):    collides with Dynamic, Kinematic
    // Dynamic(1):   collides with Static, Dynamic, Kinematic
    // Kinematic(2): collides with Dynamic
    // Trigger(3):   collides with Dynamic, Kinematic
    // Debris(4):    collides with Static

    switch (inObject1)
    {
        case Layers::STATIC:
            return inObject2 == Layers::DYNAMIC || inObject2 == Layers::KINEMATIC;
        case Layers::DYNAMIC:
            return inObject2 == Layers::STATIC || inObject2 == Layers::DYNAMIC ||
                   inObject2 == Layers::KINEMATIC || inObject2 == Layers::TRIGGER;
        case Layers::KINEMATIC:
            return inObject2 == Layers::DYNAMIC || inObject2 == Layers::STATIC;
        case Layers::TRIGGER:
            return inObject2 == Layers::DYNAMIC || inObject2 == Layers::KINEMATIC;
        case Layers::DEBRIS:
            return inObject2 == Layers::STATIC;
        default:
            return false;
    }
}

}  // namespace engine::physics
