#pragma once

#include <render/MaterialCache.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/SparseSetStore.h>

// Migration-only compatibility alias. StaticMeshComponent now lives in
// archetype chunks; this store remains only so older tests compile in Phase 2.
class StaticMeshComponentStore : public SparseSetStore<StaticMeshComponent>
{
public:
    StaticMeshComponentStore() = default;
    StaticMeshComponentStore(StaticMeshCache& meshes, MaterialCache& materials)
        : Meshes(&meshes)
        , Materials(&materials)
    {
    }

    bool Add(EntityId entity, const StaticMeshComponent& component)
    {
        const bool added = SparseSetStore<StaticMeshComponent>::Add(entity, component);
        if (added)
            Retain(component);
        return added;
    }

    bool Remove(EntityId entity)
    {
        const StaticMeshComponent* component = TryGet(entity);
        if (component != nullptr)
            Release(*component);
        return SparseSetStore<StaticMeshComponent>::Remove(entity);
    }

private:
    void Retain(const StaticMeshComponent& component)
    {
        if (Meshes != nullptr) Meshes->Retain(component.Mesh);
        if (Materials != nullptr) Materials->Retain(component.Material);
    }

    void Release(const StaticMeshComponent& component)
    {
        if (Materials != nullptr) Materials->Release(component.Material);
        if (Meshes != nullptr) Meshes->Release(component.Mesh);
    }

    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
};
