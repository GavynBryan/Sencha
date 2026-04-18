#pragma once

#include <core/batch/SparseSet.h>
#include <entity/EntityId.h>
#include <world/IComponentStore.h>
#include <sprite/SpriteComponent.h>

#include <vector>

//=============================================================================
// SpriteStore
//
// Dense storage of SpriteComponents, keyed by EntityId. The store is
// responsible for joining sprite data back to entities through SparseSet's
// parallel owner list. SpriteRenderSystem reads from this store each frame to
// submit draw calls.
//=============================================================================
class SpriteStore : public IComponentStore
{
public:
    bool Add(EntityId entity, const SpriteComponent& sprite = {})
    {
        if (!entity.IsValid())
            return false;

        Components.Emplace(entity.Index, sprite);
        return true;
    }

    bool Remove(EntityId entity)
    {
        return entity.IsValid() && Components.Remove(entity.Index);
    }

    bool Contains(EntityId entity) const
    {
        return entity.IsValid() && Components.Contains(entity.Index);
    }

    SpriteComponent* TryGet(EntityId entity)
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

    const SpriteComponent* TryGet(EntityId entity) const
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

    std::span<SpriteComponent> GetItems()
    {
        auto& items = Components.GetItems();
        return std::span<SpriteComponent>(items.data(), items.size());
    }

    std::span<const SpriteComponent> GetItems() const
    {
        const auto& items = Components.GetItems();
        return std::span<const SpriteComponent>(items.data(), items.size());
    }

    const std::vector<Id>& GetOwners() const { return Components.GetOwners(); }

    size_t Count() const { return Components.Count(); }
    bool IsEmpty() const { return Components.IsEmpty(); }
    uint64_t GetVersion() const { return Components.GetVersion(); }

private:
    SparseSet<SpriteComponent> Components;
};
