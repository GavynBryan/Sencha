#pragma once

#include <entity/EntityHandle.h>
#include <transform/TransformHierarchyService.h>
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
    EntityHandle Create()
    {
        if (!FreeIds.empty())
        {
            const EntityId id = FreeIds.back();
            FreeIds.pop_back();
            Entries[id].Alive = true;
            ++LiveCount;
            return EntityHandle{ id, Entries[id].Generation };
        }

        const EntityId id = static_cast<EntityId>(Entries.size());
        Entries.push_back(Entry{ .Generation = 1, .Alive = true });
        ++LiveCount;
        return EntityHandle{ id, 1 };
    }

    bool Destroy(EntityHandle entity)
    {
        if (!IsAlive(entity))
            return false;

        Entry& entry = Entries[entity.Id];
        entry.Alive = false;
        ++entry.Generation;
        if (entry.Generation == 0)
            ++entry.Generation;

        FreeIds.push_back(entity.Id);
        --LiveCount;
        return true;
    }

    void DestroySubtree(EntityHandle root, const TransformHierarchyService& hierarchy)
    {
        std::vector<EntityHandle> postOrder;
        CollectPostOrder(hierarchy, root, postOrder);

        for (EntityHandle entity : postOrder)
            Destroy(entity);
    }

    bool IsAlive(EntityHandle entity) const
    {
        return entity.IsValid()
            && entity.Id < Entries.size()
            && Entries[entity.Id].Alive
            && Entries[entity.Id].Generation == entity.Generation;
    }

    size_t Count() const { return LiveCount; }

private:
    struct Entry
    {
        uint16_t Generation = 1;
        bool Alive = false;
    };

    static void CollectPostOrder(
        const TransformHierarchyService& hierarchy,
        EntityHandle entity,
        std::vector<EntityHandle>& out)
    {
        for (EntityHandle child : hierarchy.GetChildren(entity))
            CollectPostOrder(hierarchy, child, out);
        out.push_back(entity);
    }

    std::vector<Entry> Entries;
    std::vector<EntityId> FreeIds;
    size_t LiveCount = 0;
};
