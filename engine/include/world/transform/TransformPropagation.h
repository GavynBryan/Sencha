#pragma once

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>

// Computes world transforms for the phase's active storage partitions.
//
// Two passes, because the two populations have different constraints. Entities
// with no parent have no ordering constraint at all, so they are swept
// chunk-linearly with the dirty test applied once per chunk. Entities with a
// parent are swept through a cached parent-before-child order, which
// PropagationOrderCache maintains and invalidates against the hierarchy rather
// than against every structural change.
//
// A partition that re-enters a domain receives one conservative full sweep;
// dormant partitions perform no propagation work.
class TransformPropagationSystem
{
public:
    explicit TransformPropagationSystem(World& world)
        : Target(world)
    {
    }

    // forceFullInvalidation rebuilds the order every sweep. It exists so a
    // suspected stale-transform bug can be bisected against the scoped
    // invalidation in one step, and so a churn scenario can be run both ways and
    // compared. It is not a fast path.
    void Propagate(
        const StoragePartitionSet& partitions,
        TransformPropagationDomain domain,
        bool forceFullInvalidation = false);

private:
    World& Target;

    std::size_t ParentCount() const;
    bool HierarchyChangedSince(uint32_t topologyFrame);
    void RebuildTopology(PropagationOrderCache& cache, std::size_t parentCount);
    void ResolveAddresses(PropagationOrderCache& cache);
    void SweepOrder(
        PropagationOrderCache& cache,
        const StoragePartitionSet& partitions,
        const PropagationSweepState& sweep,
        bool fullSweep,
        uint32_t frame);
};

inline void PropagateTransforms(
    World& world,
    const StoragePartitionSet& partitions,
    TransformPropagationDomain domain,
    bool forceFullInvalidation = false)
{
    TransformPropagationSystem propagation(world);
    propagation.Propagate(partitions, domain, forceFullInvalidation);
}

// Test/tool convenience for an isolated World where every live partition is
// intentionally active. Runtime frame code must pass the explicit phase-domain
// set above so dormant zones remain skipped at chunk granularity.
inline void PropagateTransforms(World& world)
{
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    for (EntityId entity : world.GetAliveEntities())
        partitions.Add(world.GetEntityPartition(entity));

    PropagateTransforms(
        world,
        partitions,
        TransformPropagationDomain::Simulation);
}
