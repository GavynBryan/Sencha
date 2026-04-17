#pragma once

#include <core/batch/SparseSet.h>
#include <entity/EntityHandle.h>
#include <transform/TransformPropagationOrderService.h>
#include <world/IComponentStore.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

template <typename TTransform>
struct TransformComponent
{
    TTransform Local = TTransform::Identity();
    TTransform World = TTransform::Identity();
};

//=============================================================================
// TransformStore<TTransform>
//
// Entity-indexed transform component store. A single sparse set owns paired
// local/world transforms, keyed by EntityHandle::Id. Systems sweep the dense
// component array and use the parallel owner list to join back to entities.
//=============================================================================
template <typename TTransform>
class TransformStore : public IComponentStore
{
public:
    explicit TransformStore(TransformPropagationOrderService& propagationOrder)
        : PropagationOrder(&propagationOrder)
    {
    }

    bool Add(EntityHandle entity, const TTransform& local = TTransform::Identity())
    {
        if (!entity.IsValid())
            return false;

        Components.Emplace(entity.Id, TransformComponent<TTransform>{ local, TTransform::Identity() });

        const Id index = Components.IndexOf(entity.Id);
        if (index != InvalidId)
            PropagationOrder->MarkLocalDirty(index);
        return true;
    }

    bool Remove(EntityHandle entity)
    {
        if (!entity.IsValid())
            return false;
        return Components.Remove(entity.Id);
    }

    bool Contains(EntityHandle entity) const
    {
        return entity.IsValid() && Components.Contains(entity.Id);
    }

    const TTransform* TryGetLocal(EntityHandle entity) const
    {
        const TransformComponent<TTransform>* component = TryGetComponent(entity);
        return component ? &component->Local : nullptr;
    }

    const TTransform* TryGetWorld(EntityHandle entity) const
    {
        const TransformComponent<TTransform>* component = TryGetComponent(entity);
        return component ? &component->World : nullptr;
    }

    TTransform* TryGetLocalMutable(EntityHandle entity)
    {
        TransformComponent<TTransform>* component = TryGetComponentMutable(entity);
        if (!component)
            return nullptr;

        const Id index = Components.IndexOf(entity.Id);
        if (index != InvalidId)
            PropagationOrder->MarkLocalDirty(index);
        return &component->Local;
    }

    void SetLocal(EntityHandle entity, const TTransform& local)
    {
        if (TTransform* current = TryGetLocalMutable(entity))
            *current = local;
    }

    TransformComponent<TTransform>* TryGetComponentMutable(EntityHandle entity)
    {
        if (!entity.IsValid())
            return nullptr;
        return Components.TryGet(entity.Id);
    }

    const TransformComponent<TTransform>* TryGetComponent(EntityHandle entity) const
    {
        if (!entity.IsValid())
            return nullptr;
        return Components.TryGet(entity.Id);
    }

    Id IndexOf(EntityHandle entity) const
    {
        return entity.IsValid() ? Components.IndexOf(entity.Id) : InvalidId;
    }

    std::span<TransformComponent<TTransform>> GetItems()
    {
        auto& items = Components.GetItems();
        return std::span<TransformComponent<TTransform>>(items.data(), items.size());
    }

    std::span<const TransformComponent<TTransform>> GetItems() const
    {
        const auto& items = Components.GetItems();
        return std::span<const TransformComponent<TTransform>>(items.data(), items.size());
    }

    const std::vector<Id>& GetOwners() const { return Components.GetOwners(); }

    size_t Count() const { return Components.Count(); }
    bool IsEmpty() const { return Components.IsEmpty(); }
    uint64_t GetVersion() const { return Components.GetVersion(); }

private:
    SparseSet<TransformComponent<TTransform>> Components;
    TransformPropagationOrderService* PropagationOrder = nullptr;
};
