#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include <ecs/EntityId.h>
#include <physics/PhysicsTypes.h>

class World;
class PhysicsWorld;

//=============================================================================
// Entity <-> body user-data packing
//
// A body carries its owning entity in 64 bits of backend user data so a query
// that hits the body can recover the entity. Index in the low word, generation
// in the high word.
//=============================================================================
inline uint64_t PackEntity(EntityId e)
{
    return (static_cast<uint64_t>(e.Generation) << 32) | static_cast<uint64_t>(e.Index);
}

inline EntityId UnpackEntity(uint64_t value)
{
    return EntityId{ static_cast<uint32_t>(value & 0xffffffffu), static_cast<uint32_t>(value >> 32) };
}

//=============================================================================
// PhysicsScene
//
// Per-registry ECS<->body bridge. Backend-free: it drives the PhysicsWorld
// facade, never Jolt. Stored as a World resource, so it is owned by the World
// (one per registry) and its destructor removes that zone's bodies from the
// shared PhysicsWorld when the zone unloads. The world is a raw pointer, not
// owned: it always outlives this scene because ZoneRuntime (registries + their
// PhysicsScenes) is destroyed before EngineSchedule (the PhysicsStepSystem that
// owns the world). No refcounting.
//
// Body lifetime is reconciled from component presence rather than driven by
// ComponentTraits hooks: a collider can exist before this resource does (zone
// attach runs before the first physics step) and zone unload destroys entities
// without firing OnRemove, so a hook-only scheme would miss both. Reconcile
// covers every path; see the physics plan.
//=============================================================================
class PhysicsScene
{
public:
    explicit PhysicsScene(PhysicsWorld& world);
    ~PhysicsScene();

    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;

    // Pre-step: create bodies for new colliders, drop bodies whose collider is
    // gone, and push kinematic transforms into the simulation.
    void SyncToPhysics(World& world);

    // Post-step: write dynamic bodies' resolved transforms back to LocalTransform.
    void SyncFromPhysics(World& world);

    [[nodiscard]] size_t BodyCount() const { return Bodies.size(); }

private:
    PhysicsWorld* Simulation; // not owned; outlives this scene (see above)
    std::unordered_map<EntityIndex, PhysicsBodyId> Bodies;
    std::unordered_set<EntityIndex> Seen;
};
