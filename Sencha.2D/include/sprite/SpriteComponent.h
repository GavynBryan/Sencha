#pragma once

#include <assets/texture/TextureHandle.h>
#include <core/batch/SparseSet.h>
#include <entity/EntityHandle.h>
#include <world/IComponentStore.h>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// SpriteComponent
//
// Pure visual data for a 2D entity. Transform ownership lives in
// TransformStore<Transform2f>; SpriteStore joins sprite data back to entities
// through SparseSet's parallel owner list.
//=============================================================================
struct SpriteComponent
{
    TextureHandle Texture;
    float         Width   = 16.0f;
    float         Height  = 16.0f;
    uint32_t      Color   = 0xFFFFFFFFu;
    int32_t       SortKey = 0;
};

class SpriteStore : public IComponentStore
{
public:
    bool Add(EntityHandle entity, const SpriteComponent& sprite = {})
    {
        if (!entity.IsValid())
            return false;

        Components.Emplace(entity.Id, sprite);
        return true;
    }

    bool Remove(EntityHandle entity)
    {
        return entity.IsValid() && Components.Remove(entity.Id);
    }

    bool Contains(EntityHandle entity) const
    {
        return entity.IsValid() && Components.Contains(entity.Id);
    }

    SpriteComponent* TryGet(EntityHandle entity)
    {
        return entity.IsValid() ? Components.TryGet(entity.Id) : nullptr;
    }

    const SpriteComponent* TryGet(EntityHandle entity) const
    {
        return entity.IsValid() ? Components.TryGet(entity.Id) : nullptr;
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
