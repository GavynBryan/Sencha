#pragma once

#include <ecs/Query.h>
#include <ecs/World.h>
#include <world/transform/TransformComponents.h>
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

    void Propagate();

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

    auto items = transforms.GetItems();
    for (const TransformPropagationOrderService::PropagationEntry& entry : cache.GetOrder())
    {
        if (entry.TransformIndex >= items.size())
            continue;

        auto& transform = items[entry.TransformIndex];
        if (entry.ParentTransformIndex == TransformPropagationOrderService::NullIndex
            || entry.ParentTransformIndex >= items.size())
        {
            transform.World = transform.Local;
        }
        else
        {
            transform.World = items[entry.ParentTransformIndex].World * transform.Local;
        }
    }

    cache.GetLocalDirty().ClearAll();
    cache.GetWorldChanged().ClearAll();
}

// Restores world-transform coherence for every unique registry in the span.
// Stubbed in Phase 2; full propagation logic is restored in Phase 3.
void PropagateTransforms(std::span<Registry*> registries);
