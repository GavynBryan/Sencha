#pragma once

#include <ecs/Query.h>
#include <ecs/World.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

#include <span>

class Registry;

//=============================================================================
// TransformPropagationSystem
//
// Propagates LocalTransform → WorldTransform for all entities that have both
// components, respecting the spatial hierarchy expressed by the Parent component.
//
// Uses a PropagationOrderCache World resource: a parent-before-child dense
// ordered list rebuilt only when Changed<Parent> signals a structural hierarchy
// change. Each frame the sweep is a single forward pass with no hash lookups.
//
// See docs/ecs/decisions.md D3.1 for the mandate and benchmark rationale.
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

    // Rebuilds the PropagationOrderCache from the current Parent graph.
    // Called when the cache is dirty (Changed<Parent> fired, or first frame).
    void RebuildCache(PropagationOrderCache& cache);
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
void PropagateTransforms(std::span<Registry*> registries);
