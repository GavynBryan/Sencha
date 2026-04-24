#pragma once

#include <ecs/EntityId.h>
#include <world/transform/TransformHierarchyService.h>
#include <cstddef>
#include <cstdint>
#include <vector>

//=============================================================================
// EntityRegistry
//
// Lightweight handle allocator for the hybrid ECS path. Component lifetime is
// explicit in SparseSet-backed stores; the registry only owns id/generation
// liveness and provides hierarchy-aware destruction ordering.
//=============================================================================
class EntityRegistry
{
public:
    EntityId Create()
    {
        if (!FreeIds.empty())
        {
            const EntityIndex index = FreeIds.back();
            FreeIds.pop_back();
            Entries[index].Alive = true;
            ++LiveCount;
            return EntityId{ index, Entries[index].Generation };
        }

        const EntityIndex index = static_cast<EntityIndex>(Entries.size());
        Entries.push_back(Entry{ .Generation = 1, .Alive = true });
        ++LiveCount;
        return EntityId{ index, 1 };
    }

    bool Destroy(EntityId entity)
    {
        if (!IsAlive(entity))
            return false;

        Entry& entry = Entries[entity.Index];
        entry.Alive = false;
        ++entry.Generation;
        if (entry.Generation == 0)
            ++entry.Generation;

        FreeIds.push_back(entity.Index);
        --LiveCount;
        return true;
    }

    void DestroySubtree(EntityId root, const TransformHierarchyService& hierarchy)
    {
        std::vector<EntityId> postOrder;
        CollectPostOrder(hierarchy, root, postOrder);

        for (EntityId entity : postOrder)
            Destroy(entity);
    }

    bool IsAlive(EntityId entity) const
    {
        return entity.IsValid()
            && entity.Index < Entries.size()
            && Entries[entity.Index].Alive
            && Entries[entity.Index].Generation == entity.Generation;
    }

    size_t Count() const { return LiveCount; }

    std::vector<EntityId> GetAliveEntities() const
    {
        std::vector<EntityId> entities;
        entities.reserve(LiveCount);

        for (EntityIndex index = 0; index < Entries.size(); ++index)
        {
            const Entry& entry = Entries[index];
            if (entry.Alive)
                entities.push_back(EntityId{ index, entry.Generation });
        }

        return entities;
    }

private:
    struct Entry
    {
        uint16_t Generation = 1;
        bool Alive = false;
    };

    static void CollectPostOrder(
        const TransformHierarchyService& hierarchy,
        EntityId entity,
        std::vector<EntityId>& out)
    {
        for (EntityId child : hierarchy.GetChildren(entity))
            CollectPostOrder(hierarchy, child, out);
        out.push_back(entity);
    }

    std::vector<Entry> Entries;
    std::vector<EntityIndex> FreeIds;
    size_t LiveCount = 0;
};
