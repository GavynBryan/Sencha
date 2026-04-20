#pragma once

#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

#include <span>

class Registry;

//=============================================================================
// TransformPropagationSystem
//
// Derives world transforms from entity-owned local transforms by walking the
// transform hierarchy in a cached parent-before-child order.
//=============================================================================
template <typename TTransform>
class TransformPropagationSystem
{
public:
    TransformPropagationSystem(
        TransformStore<TTransform>& transforms,
        TransformHierarchyService& hierarchy,
        TransformPropagationOrderService& cache)
        : Transforms(transforms)
        , Hierarchy(hierarchy)
        , Cache(cache)
    {
    }

    void Propagate()
    {
        Cache.MaybeRebuild(Hierarchy, Transforms);

        std::span<const TransformPropagationOrderService::PropagationEntry> order = Cache.GetOrder();
        if (order.empty())
            return;

        if (Cache.IsAllClean())
            return;

        std::span<TransformComponent<TTransform>> components = Transforms.GetItems();
        DenseBitset& localDirty = Cache.GetLocalDirty();
        DenseBitset& worldChanged = Cache.GetWorldChanged();

        worldChanged.ClearAll();

        for (const TransformPropagationOrderService::PropagationEntry& entry : order)
        {
            const bool localDirtyFlag = localDirty.Test(entry.TransformIndex);
            const bool parentChanged =
                entry.ParentTransformIndex != TransformPropagationOrderService::NullIndex
                && worldChanged.Test(entry.ParentTransformIndex);

            if (!localDirtyFlag && !parentChanged)
                continue;

            TransformComponent<TTransform>& component = components[entry.TransformIndex];
            if (entry.ParentTransformIndex == TransformPropagationOrderService::NullIndex)
            {
                component.World = component.Local;
            }
            else
            {
                component.World =
                    components[entry.ParentTransformIndex].World * component.Local;
            }

            worldChanged.Set(entry.TransformIndex);
        }

        localDirty.ClearAll();
    }

    void Tick(float /*fixedDt*/)
    {
        Propagate();
    }

private:
    TransformStore<TTransform>& Transforms;
    TransformHierarchyService& Hierarchy;
    TransformPropagationOrderService& Cache;
};

//=============================================================================
// PropagateTransforms
//
// Convenience entry point for one-shot propagation over explicit transform
// services.
//=============================================================================
template <typename TTransform>
void PropagateTransforms(
    TransformStore<TTransform>& transforms,
    TransformHierarchyService& hierarchy,
    TransformPropagationOrderService& cache)
{
    TransformPropagationSystem<TTransform> propagation(transforms, hierarchy, cache);
    propagation.Propagate();
}

//=============================================================================
// PropagateTransforms
//
// Restores world-transform coherence for every unique registry in the span that
// owns the default 3D transform resources.
//=============================================================================
void PropagateTransforms(std::span<Registry*> registries);
