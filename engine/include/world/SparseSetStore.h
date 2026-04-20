#pragma once

#include <core/batch/SparseSet.h>
#include <world/ITypedComponentStore.h>

#include <span>

//=============================================================================
// SparseSetStore<T>
//
// Default entity-indexed component store. Components are packed densely for
// cache-friendly iteration, with a parallel owner-id array for entity joins.
//=============================================================================
template <typename T>
class SparseSetStore : public ITypedComponentStore<T>
{
public:
    bool Add(EntityId entity, const T& component) override
    {
        if (!entity.IsValid())
            return false;

        Components.Emplace(entity.Index, component);
        return true;
    }

    bool Remove(EntityId entity) override
    {
        return entity.IsValid() && Components.Remove(entity.Index);
    }

    [[nodiscard]] bool Contains(EntityId entity) const
    {
        return entity.IsValid() && Components.Contains(entity.Index);
    }

    [[nodiscard]] T* TryGet(EntityId entity) override
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

    [[nodiscard]] const T* TryGet(EntityId entity) const override
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

    [[nodiscard]] T* TryGetMutable(EntityId entity)
    {
        return TryGet(entity);
    }

    [[nodiscard]] std::span<T> GetItems() override
    {
        auto& items = Components.GetItems();
        return { items.data(), items.size() };
    }

    [[nodiscard]] std::span<const T> GetItems() const override
    {
        const auto& items = Components.GetItems();
        return { items.data(), items.size() };
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
    SparseSet<T> Components;
};
