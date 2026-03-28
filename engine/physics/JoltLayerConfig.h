#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace engine::physics
{

// Object layers
namespace Layers
{
inline constexpr JPH::ObjectLayer STATIC = 0;
inline constexpr JPH::ObjectLayer DYNAMIC = 1;
inline constexpr JPH::ObjectLayer KINEMATIC = 2;
inline constexpr JPH::ObjectLayer TRIGGER = 3;
inline constexpr JPH::ObjectLayer DEBRIS = 4;
inline constexpr JPH::ObjectLayer NUM_LAYERS = 5;
}  // namespace Layers

// Broad-phase layers (coarser grouping)
namespace BroadPhaseLayers
{
inline constexpr JPH::BroadPhaseLayer NON_MOVING(0);
inline constexpr JPH::BroadPhaseLayer MOVING(1);
inline constexpr uint32_t NUM_LAYERS = 2;
}  // namespace BroadPhaseLayers

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BroadPhaseLayerInterfaceImpl();

    JPH::uint GetNumBroadPhaseLayers() const override;
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
};

}  // namespace engine::physics
