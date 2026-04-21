#pragma once

#include <core/batch/SparseSet.h>
#include <render/MaterialCache.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/ITypedComponentStore.h>

#include <cstddef>
#include <cstdint>
#include <span>

//=============================================================================
// StaticMeshComponentStore
//
// Component store for static mesh components. When asset caches are supplied, stored
// handles are retained on add/replace and released on remove/destruction.
//=============================================================================
class StaticMeshComponentStore final : public ITypedComponentStore<StaticMeshComponent>
{
public:
    StaticMeshComponentStore() = default;
    StaticMeshComponentStore(StaticMeshCache& meshes, MaterialCache& materials);
    ~StaticMeshComponentStore() override;

    StaticMeshComponentStore(const StaticMeshComponentStore&) = delete;
    StaticMeshComponentStore& operator=(const StaticMeshComponentStore&) = delete;
    StaticMeshComponentStore(StaticMeshComponentStore&&) = delete;
    StaticMeshComponentStore& operator=(StaticMeshComponentStore&&) = delete;

    void SetAssetCaches(StaticMeshCache& meshes, MaterialCache& materials);

    bool Add(EntityId entity, const StaticMeshComponent& component) override;
    bool Remove(EntityId entity) override;

    [[nodiscard]] bool Contains(EntityId entity) const;
    [[nodiscard]] StaticMeshComponent* TryGet(EntityId entity) override;
    [[nodiscard]] const StaticMeshComponent* TryGet(EntityId entity) const override;
    [[nodiscard]] StaticMeshComponent* TryGetMutable(EntityId entity);

    [[nodiscard]] std::span<StaticMeshComponent> GetItems() override;
    [[nodiscard]] std::span<const StaticMeshComponent> GetItems() const override;
    [[nodiscard]] std::span<const EntityIndex> GetOwnerIds() const override;

    [[nodiscard]] size_t Count() const override;
    [[nodiscard]] bool IsEmpty() const override;
    [[nodiscard]] uint64_t GetVersion() const override;

private:
    void Attach(const StaticMeshComponent& component);
    void Detach(const StaticMeshComponent& component);

    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    SparseSet<StaticMeshComponent> Components;
};
