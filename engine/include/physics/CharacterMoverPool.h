#pragma once

#include <cstdint>
#include <memory>

#include <math/Vec.h>

class World;
class PhysicsWorld;

//=============================================================================
// CharacterMoverPool
//
// Per-registry ECS<->character bridge, the character analogue of PhysicsScene.
// A CharacterMover owns a Jolt CharacterVirtual and is not trivially copyable,
// so it cannot live in a chunk; the movers live here in a dense pool with a free
// list (so a slot stays valid for the mover's lifetime) and each controller
// entity carries its slot in a CharacterMoverLink component. Stored as a World
// resource, so it dies with the zone and releases its CharacterVirtuals; the
// shared PhysicsWorld it points at outlives it (same teardown order as bodies).
//
// Reconcile is gated on the World's structural version, like PhysicsScene, so a
// steady frame skips topology work; the pool itself is the physics-side record
// that lets a destroyed entity's mover be reclaimed (DestroyEntity fires no hook
// and the link vanishes with the entity).
//=============================================================================
class CharacterMoverPool
{
public:
    explicit CharacterMoverPool(PhysicsWorld& world);
    ~CharacterMoverPool();

    CharacterMoverPool(const CharacterMoverPool&) = delete;
    CharacterMoverPool& operator=(const CharacterMoverPool&) = delete;

    // Create movers for new controllers and release movers for dead or removed
    // ones. Gated on the structural version: a steady frame is a single compare.
    void Reconcile(World& world);

    // Advance every linked mover one tick and write resolved position + grounded
    // back. Contiguous column walk over (CharacterController, LocalTransform,
    // CharacterMoverLink); the only indirection is the slot into the pool.
    void Drive(World& world, float dt, const Vec3d& gravity);

    [[nodiscard]] size_t   MoverCount() const;
    [[nodiscard]] uint64_t ReconcilePasses() const { return ReconcileCount; }

private:
    struct State; // PIMPL: pool slots + free list + cached query + command buffer

    bool   Ready(const World& world) const;
    State& EnsureState(World& world);

    PhysicsWorld*          Simulation; // not owned; outlives this pool
    std::unique_ptr<State> S;
    uint64_t               LastStructuralVersion = 0;
    uint64_t               ReconcileCount = 0;
};
