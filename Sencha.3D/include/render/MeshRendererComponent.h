#pragma once

#include <core/batch/SparseSet.h>
#include <entity/EntityId.h>
#include <render/Material.h>
#include <render/MeshTypes.h>
#include <world/IComponentStore.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// MeshRendererComponent
//
// ECS component that pairs an entity with a mesh and material. LayerMask and
// SubmeshMask are bitmasks: a cleared bit skips the corresponding layer or
// submesh during extraction.
//=============================================================================
struct MeshRendererComponent
{
    MeshHandle Mesh;
    MaterialHandle Material;
    bool Visible = true;
    uint32_t LayerMask = 0xFFFFFFFFu;
    uint32_t SubmeshMask = 0xFFFFFFFFu;
};

//=============================================================================
// MeshRendererStore
//
// IComponentStore that maps EntityId -> MeshRendererComponent. Backed by a
// SparseSet. GetItems() and GetOwners() return parallel spans used by
// RenderExtractionSystem to iterate all renderers without entity lookups.
//=============================================================================
class MeshRendererStore : public IComponentStore
{
public:
    bool Add(EntityId entity, const MeshRendererComponent& component)
    {
        if (!entity.IsValid()) return false;
        Components.Emplace(entity.Index, component);
        return true;
    }

    bool Remove(EntityId entity)
    {
        return entity.IsValid() && Components.Remove(entity.Index);
    }

    [[nodiscard]] const MeshRendererComponent* TryGet(EntityId entity) const
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

    [[nodiscard]] MeshRendererComponent* TryGetMutable(EntityId entity)
    {
        return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
    }

    [[nodiscard]] std::span<const MeshRendererComponent> GetItems() const
    {
        const auto& items = Components.GetItems();
        return { items.data(), items.size() };
    }

    [[nodiscard]] const std::vector<Id>& GetOwners() const { return Components.GetOwners(); }

private:
    SparseSet<MeshRendererComponent> Components;
};
