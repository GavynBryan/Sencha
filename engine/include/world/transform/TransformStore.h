#pragma once

#include <core/batch/SparseSet.h>
#include <world/entity/EntityId.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/ITypedComponentStore.h>
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
// local/world transforms, keyed by EntityId::Index. Systems sweep the dense
// component array and use the parallel owner list to join back to entities.
//=============================================================================
template <typename TTransform>
class TransformStore : public ITypedComponentStore<TransformComponent<TTransform>>
{
public:
    explicit TransformStore(TransformPropagationOrderService& propagationOrder)
        : PropagationOrder(&propagationOrder)
    {
    }

    bool Add(EntityId entity, const TTransform& local = TTransform::Identity())
    {
        return Add(entity, TransformComponent<TTransform>{ local, TTransform::Identity() });
    }

    bool Add(EntityId entity, const TransformComponent<TTransform>& component) override
    {
        if (!entity.IsValid())
            return false;

        Components.Emplace(entity.Index, component);

        const Id index = Components.IndexOf(entity.Index);
        if (index != InvalidId)
            PropagationOrder->MarkLocalDirty(index);
        return true;
    }

    bool Remove(EntityId entity) override
    {
        if (!entity.IsValid())
            return false;
        return Components.Remove(entity.Index);
    }

    bool Contains(EntityId entity) const
    {
        return entity.IsValid() && Components.Contains(entity.Index);
    }

    const TTransform* TryGetLocal(EntityId entity) const
    {
        const TransformComponent<TTransform>* component = TryGetComponent(entity);
        return component ? &component->Local : nullptr;
    }

    const TTransform* TryGetWorld(EntityId entity) const
    {
        const TransformComponent<TTransform>* component = TryGetComponent(entity);
        return component ? &component->World : nullptr;
    }

    TTransform* TryGetLocalMutable(EntityId entity)
    {
        TransformComponent<TTransform>* component = TryGetComponentMutable(entity);
        if (!component)
            return nullptr;

        const Id index = Components.IndexOf(entity.Index);
        if (index != InvalidId)
            PropagationOrder->MarkLocalDirty(index);
        return &component->Local;
    }

    void SetLocal(EntityId entity, const TTransform& local)
    {
        if (TTransform* current = TryGetLocalMutable(entity))
            *current = local;
    }

    TransformComponent<TTransform>* TryGetComponentMutable(EntityId entity)
    {
        if (!entity.IsValid())
            return nullptr;
        return Components.TryGet(entity.Index);
    }

    const TransformComponent<TTransform>* TryGetComponent(EntityId entity) const
    {
        if (!entity.IsValid())
            return nullptr;
        return Components.TryGet(entity.Index);
    }

    Id IndexOf(EntityId entity) const
    {
        return entity.IsValid() ? Components.IndexOf(entity.Index) : InvalidId;
    }

    TransformComponent<TTransform>* TryGet(EntityId entity) override
    {
        return TryGetComponentMutable(entity);
    }

    const TransformComponent<TTransform>* TryGet(EntityId entity) const override
    {
        return TryGetComponent(entity);
    }

    std::span<TransformComponent<TTransform>> GetItems() override
    {
        auto& items = Components.GetItems();
        return std::span<TransformComponent<TTransform>>(items.data(), items.size());
    }

    std::span<const TransformComponent<TTransform>> GetItems() const override
    {
        const auto& items = Components.GetItems();
        return std::span<const TransformComponent<TTransform>>(items.data(), items.size());
    }

    [[nodiscard]] std::span<const EntityIndex> GetOwnerIds() const override
    {
        const auto& owners = Components.GetOwners();
        return { owners.data(), owners.size() };
    }

    [[nodiscard]] size_t Count() const override { return Components.Count(); }
    [[nodiscard]] bool IsEmpty() const override { return Components.IsEmpty(); }
    [[nodiscard]] uint64_t GetVersion() const override { return Components.GetVersion(); }

private:
    SparseSet<TransformComponent<TTransform>> Components;
    TransformPropagationOrderService* PropagationOrder = nullptr;
};
