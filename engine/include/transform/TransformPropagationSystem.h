#pragma once

#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationOrderService.h>
#include <transform/TransformStore.h>
#include <span>

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
