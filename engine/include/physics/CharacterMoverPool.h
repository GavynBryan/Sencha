#pragma once

#include <cstdint>
#include <memory>

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <math/Vec.h>

class PhysicsWorld;
class StoragePartitionSet;
class World;

// Simulation-wide pool for CharacterMover backend objects. One pool serves the
// unified runtime World; storage partitions only control which movers are
// resident and driven during the current physics domain.
class CharacterMoverPool
{
public:
    explicit CharacterMoverPool(PhysicsWorld& world);
    ~CharacterMoverPool();

    CharacterMoverPool(const CharacterMoverPool&) = delete;
    CharacterMoverPool& operator=(const CharacterMoverPool&) = delete;

    void Reconcile(
        World& world,
        const StoragePartitionSet& partitions);
    void Drive(
        World& world,
        const StoragePartitionSet& partitions,
        float dt,
        const Vec3d& gravity);

    void EvictPartition(
        World& world,
        StoragePartitionId partition);
    void EvictAll(World& world);

    // Move an existing character and its authored transform atomically. Used
    // by world-partition threshold clamps and explicit gameplay teleports so
    // the next physics tick cannot restore the pre-clamp position.
    [[nodiscard]] bool SetPosition(World& world, EntityId entity,
                                   const Vec3d& position);

    [[nodiscard]] size_t MoverCount() const;
    [[nodiscard]] uint64_t ReconcilePasses() const
    {
        return ReconcileCount;
    }

private:
    struct State;

    bool Ready(const World& world) const;
    State& EnsureState(World& world);

    PhysicsWorld* Simulation;
    std::unique_ptr<State> S;
    uint64_t LastStructuralVersion = 0;
    uint64_t ReconcileCount = 0;
};
