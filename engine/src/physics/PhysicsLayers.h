#pragma once

// Internal Jolt-side header: included only by physics .cpp files, never by a
// public engine header. Defines the broad/object layer scheme behind
// CollisionLayer and the three filter interfaces PhysicsSystem::Init requires.

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <physics/PhysicsTypes.h>

// Object layers: one per CollisionLayer, same ordinal so conversion is a cast.
namespace PhysicsObjectLayers
{
    static constexpr JPH::ObjectLayer Static = 0;
    static constexpr JPH::ObjectLayer Moving = 1;
    static constexpr JPH::ObjectLayer Character = 2;
    static constexpr JPH::ObjectLayer Trigger = 3;
    static constexpr JPH::ObjectLayer Count = 4;
}

// Broadphase layers: the two coarse buckets the trees are split into.
namespace PhysicsBroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NonMoving(0);
    static constexpr JPH::BroadPhaseLayer Moving(1);
    static constexpr JPH::uint Count = 2;
}

inline JPH::ObjectLayer ToObjectLayer(CollisionLayer layer)
{
    return static_cast<JPH::ObjectLayer>(layer);
}

// Which object layers test against each other. Symmetric. Static never pairs
// with Static; Trigger reports overlaps with the things that can enter it.
class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        switch (a)
        {
        case PhysicsObjectLayers::Static:
            return b == PhysicsObjectLayers::Moving || b == PhysicsObjectLayers::Character;
        case PhysicsObjectLayers::Moving:
            return true;
        case PhysicsObjectLayers::Character:
            return b == PhysicsObjectLayers::Static || b == PhysicsObjectLayers::Moving
                || b == PhysicsObjectLayers::Trigger;
        case PhysicsObjectLayers::Trigger:
            return b == PhysicsObjectLayers::Moving || b == PhysicsObjectLayers::Character;
        default:
            return false;
        }
    }
};

// Maps each object layer to its broadphase bucket. Non-moving things (static
// geometry, triggers) go in one tree, movers in the other.
class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return PhysicsBroadPhaseLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        switch (layer)
        {
        case PhysicsObjectLayers::Moving:
        case PhysicsObjectLayers::Character:
            return PhysicsBroadPhaseLayers::Moving;
        default:
            return PhysicsBroadPhaseLayers::NonMoving;
        }
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == PhysicsBroadPhaseLayers::Moving ? "Moving" : "NonMoving";
    }
#endif
};

// Whether an object layer should test against a broadphase bucket.
class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
    {
        switch (layer)
        {
        case PhysicsObjectLayers::Static:
        case PhysicsObjectLayers::Trigger:
            return bp == PhysicsBroadPhaseLayers::Moving;
        case PhysicsObjectLayers::Moving:
        case PhysicsObjectLayers::Character:
            return true;
        default:
            return false;
        }
    }
};
