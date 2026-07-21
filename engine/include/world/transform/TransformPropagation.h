#pragma once

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>

//=============================================================================
// TransformPropagationSystem
//
// Caches one parent-before-child order for the unified World and filters the
// steady-state sweep by the phase's active storage partitions. A partition that
// re-enters a propagation domain receives one conservative full sweep; dormant
// partitions perform no propagation work.
//=============================================================================
class TransformPropagationSystem
{
public:
    explicit TransformPropagationSystem(World& world)
        : Target(world)
    {
    }

    void Propagate(
        const StoragePartitionSet& partitions,
        TransformPropagationDomain domain);

private:
    World& Target;
    void RebuildCache(PropagationOrderCache& cache);
};

inline void PropagateTransforms(
    World& world,
    const StoragePartitionSet& partitions,
    TransformPropagationDomain domain)
{
    TransformPropagationSystem propagation(world);
    propagation.Propagate(partitions, domain);
}
