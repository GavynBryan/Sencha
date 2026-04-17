#pragma once

#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>
#include <cstdint>
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
    using ComponentType = TransformComponent<TTransform>;
    using PropagationEntry = TransformPropagationOrderService::PropagationEntry;

    static constexpr uint32_t NullIndex = TransformPropagationOrderService::NullIndex;

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

        std::span<const PropagationEntry> order = Cache.GetOrder();
        if (order.empty())
            return;

        if (Cache.IsAllClean())
            return;

        std::span<ComponentType> components = Transforms.GetItems();
        DenseBitset& localDirty = Cache.GetLocalDirty();
        DenseBitset& worldChanged = Cache.GetWorldChanged();

        worldChanged.ClearAll();

        for (const PropagationEntry& entry : order)
        {
            const bool localDirtyFlag = localDirty.Test(entry.TransformIndex);
            const bool parentChanged =
                entry.ParentTransformIndex != NullIndex
                && worldChanged.Test(entry.ParentTransformIndex);

            if (!localDirtyFlag && !parentChanged)
                continue;

            ComponentType& component = components[entry.TransformIndex];
            if (entry.ParentTransformIndex == NullIndex)
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
