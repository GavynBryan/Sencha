#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <physics/PhysicsTypes.h>

class PhysicsWorld;
class StoragePartitionSet;
class World;

inline uint64_t PackEntity(EntityId entity)
{
    return (static_cast<uint64_t>(entity.Generation) << 32)
        | static_cast<uint64_t>(entity.Index);
}

inline EntityId UnpackEntity(uint64_t value)
{
    return EntityId{
        static_cast<uint32_t>(value & 0xffffffffu),
        static_cast<uint32_t>(value >> 32),
    };
}

// Simulation-wide ECS-to-body bridge. One instance owns every backend rigid
// body for the unified runtime World. Storage partitions control backend
// residency; they are not separate scenes or separate binding owners.
class RigidBodyBinding
{
public:
    explicit RigidBodyBinding(PhysicsWorld& world);
    ~RigidBodyBinding();

    RigidBodyBinding(const RigidBodyBinding&) = delete;
    RigidBodyBinding& operator=(const RigidBodyBinding&) = delete;

    void SyncToPhysics(
        World& world,
        const StoragePartitionSet& partitions);
    void SyncFromPhysics(
        World& world,
        const StoragePartitionSet& partitions);

    // Final owner-thread residency action for one partition. Dynamic state is
    // captured before bodies are removed and PhysicsBodyLink components are
    // stripped, so re-entry recreates from component-authoritative state.
    void EvictPartition(
        World& world,
        StoragePartitionId partition);
    void EvictAll(World& world);

    [[nodiscard]] size_t BodyCount() const { return Owned.size(); }
    [[nodiscard]] uint64_t ReconcilePasses() const
    {
        return ReconcileCount;
    }

private:
    struct BodyRecord
    {
        EntityId Entity;
        PhysicsBodyId Body;
    };

    struct SceneState;

    bool Ready(const World& world) const;
    SceneState& EnsureState(World& world);
    void Reconcile(
        World& world,
        SceneState& state,
        const StoragePartitionSet& partitions);
    void CaptureDynamicState(
        World& world,
        const BodyRecord& record);

    PhysicsWorld* Simulation;
    std::vector<BodyRecord> Owned;
    std::unique_ptr<SceneState> State;
    uint64_t LastStructuralVersion = 0;
    uint64_t ReconcileCount = 0;
};
