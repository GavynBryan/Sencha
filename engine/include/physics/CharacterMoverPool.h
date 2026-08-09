#pragma once

#include <cstdint>
#include <memory>

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <math/Vec.h>

class CharacterMover;
class PhysicsWorld;
class StoragePartitionSet;
class World;
struct KinematicState;
struct LocalTransform;
struct MotionRequest;
struct SupportState;

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

    // Advance one character one step from an explicit request, writing back the
    // same state Drive writes. Prediction replay re-runs the ticks a client has
    // not had answered yet, and it has to move the pawn through the code the
    // live tick uses rather than through an approximation of it -- two rulebooks
    // for one character is the divergence it exists to remove.
    //
    // Safe to call repeatedly outside the physics step: characters carry no
    // broadphase presence, so a sweep sees static geometry and nothing else.
    [[nodiscard]] bool Step(World& world, EntityId entity,
                            const MotionRequest& request, float dt,
                            const Vec3d& gravity);

    // Put a character back where the authority says it was, without the
    // presentation snap. Reconciliation is not a teleport: the pawn is being
    // returned to a position it is already near, the replay that follows moves
    // it from there, and blending across the residual is the correct look.
    [[nodiscard]] bool RestorePosition(World& world, EntityId entity,
                                       const Vec3d& position);

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
    bool ReadyToDrive(const World& world) const;
    State& EnsureState(World& world);
    // The one place a character is swept and its results written back. Drive
    // calls it per row of a chunk, Step calls it for one entity; nothing else
    // moves a character.
    static void Sweep(CharacterMover& mover, const MotionRequest& request,
                      float dt, const Vec3d& gravity, KinematicState& kinematics,
                      SupportState& support, LocalTransform& transform);

    PhysicsWorld* Simulation;
    std::unique_ptr<State> S;
    uint64_t LastStructuralVersion = 0;
    uint64_t ReconcileCount = 0;
};
