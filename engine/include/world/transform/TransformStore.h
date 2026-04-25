#pragma once

#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/SparseSetStore.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>

//=============================================================================
// TransformComponent<TTransform>
//
// Migration-only compatibility bundle. The archetype ECS split is
// LocalTransform + WorldTransform + Parent.
//=============================================================================
template <typename TTransform>
struct TransformComponent
{
    TTransform Local = TTransform::Identity();
    TTransform World = TTransform::Identity();
};

template <typename TTransform>
class TransformStore : public SparseSetStore<TransformComponent<TTransform>>
{
public:
    explicit TransformStore(TransformPropagationOrderService& order)
        : Order(&order)
    {
    }

    bool Add(EntityId entity, const TTransform& local = TTransform::Identity())
    {
        const bool added = Base::Add(
            entity,
            TransformComponent<TTransform>{ local, TTransform::Identity() });
        if (added && Order != nullptr)
            Order->MarkLocalDirty(Base::IndexOf(entity));
        return added;
    }

    bool Add(EntityId entity, const TransformComponent<TTransform>& component)
    {
        const bool added = Base::Add(entity, component);
        if (added && Order != nullptr)
            Order->MarkLocalDirty(Base::IndexOf(entity));
        return added;
    }

    bool Contains(EntityId entity) const
    {
        return Base::TryGet(entity) != nullptr;
    }

    TTransform* TryGetLocalMutable(EntityId entity)
    {
        auto* component = Base::TryGet(entity);
        if (component == nullptr)
            return nullptr;
        if (Order != nullptr)
            Order->MarkLocalDirty(Base::IndexOf(entity));
        return &component->Local;
    }

    bool SetLocal(EntityId entity, const TTransform& local)
    {
        auto* component = Base::TryGet(entity);
        if (component == nullptr)
            return false;

        component->Local = local;
        component->World = local;
        if (Order != nullptr)
            Order->MarkLocalDirty(Base::IndexOf(entity));
        return true;
    }

    const TTransform* TryGetLocal(EntityId entity) const
    {
        const auto* component = Base::TryGet(entity);
        return component == nullptr ? nullptr : &component->Local;
    }

    const TTransform* TryGetWorld(EntityId entity) const
    {
        const auto* component = Base::TryGet(entity);
        return component == nullptr ? nullptr : &component->World;
    }

private:
    using Base = SparseSetStore<TransformComponent<TTransform>>;

    TransformPropagationOrderService* Order = nullptr;
};

// Legacy propagation function — used by TransformHierarchyStressTest and
// TransformServiceTests as a pre-migration reference baseline.
// Do not use in new production code; use PropagateTransforms(World&) instead.
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
