#pragma once

#include <ecs/World.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

#include <span>

class Registry;

//=============================================================================
// TransformPropagationSystem
//
// Phase-2 placeholder for the archetype ECS transform propagation system.
// Phase 3 ports the parent-before-child propagation logic to queries over
// LocalTransform, WorldTransform, and Parent.
//=============================================================================
class TransformPropagationSystem
{
public:
    explicit TransformPropagationSystem(World& world)
        : Target(world)
    {
    }

    void Propagate()
    {
        (void)Target;
    }

    void Tick(float /*fixedDt*/)
    {
        Propagate();
    }

private:
    World& Target;
};

inline void PropagateTransforms(World& world)
{
    TransformPropagationSystem propagation(world);
    propagation.Propagate();
}

template <typename TTransform>
void PropagateTransforms(
    TransformStore<TTransform>& transforms,
    TransformHierarchyService& hierarchy,
    TransformPropagationOrderService& cache)
{
    cache.MaybeRebuild(hierarchy, transforms);
}

// Restores world-transform coherence for every unique registry in the span.
// Stubbed in Phase 2; full propagation logic is restored in Phase 3.
void PropagateTransforms(std::span<Registry*> registries);
