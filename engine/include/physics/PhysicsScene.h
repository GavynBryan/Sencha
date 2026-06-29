#pragma once

#include <cstdint>
#include <memory>
#include <vector>

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
// Steady-state cost is proportional to what moved, not what exists. The body
// handle lives in a per-entity PhysicsBodyLink component, so the per-frame
// transform sync is a contiguous column walk with no hashing. Body topology is
// reconciled only when the World's structural version changes (an entity or
// component was created/destroyed/added/removed); a steady frame is a single
// integer compare. The dense Owned vector is the physics-side record that makes
// destroy-detection possible: DestroyEntity fires no hook and the entity's
// PhysicsBodyLink vanishes with it, so nothing in the ECS can report the dead
// body. Reconcile sweeps Owned for dead or collider-less entities.
//=============================================================================
class PhysicsScene
{
public:
    explicit PhysicsScene(PhysicsWorld& world);
    ~PhysicsScene();

    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;

    // Pre-step: reconcile body topology (gated on the structural version), then
    // push kinematic transforms into the simulation.
    void SyncToPhysics(World& world);

    // Post-step: write dynamic bodies' resolved transforms back to LocalTransform.
    void SyncFromPhysics(World& world);

    [[nodiscard]] size_t BodyCount() const { return Owned.size(); }

    // Times the topology reconcile pass has run. A steady frame (no structural
    // change in this zone) does not advance it; tests and profiling use it to
    // confirm the gate holds.
    [[nodiscard]] uint64_t ReconcilePasses() const { return ReconcileCount; }

private:
    struct BodyRecord
    {
        EntityId      Entity;
        PhysicsBodyId Body;
    };

    struct SceneState; // PIMPL: cached queries + reusable command buffer (ECS-side)

    bool        Ready(const World& world) const;
    SceneState& EnsureState(World& world);
    void        Reconcile(World& world, SceneState& state);

    PhysicsWorld*               Simulation; // not owned; outlives this scene (see above)
    std::vector<BodyRecord>     Owned;
    std::unique_ptr<SceneState> State;
    uint64_t                    LastStructuralVersion = 0;
    uint64_t                    ReconcileCount = 0;
};
