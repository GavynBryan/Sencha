#pragma once

#include <core/batch/SparseSet.h>
#include <render/MaterialCache.h>
#include <render/MeshCache.h>
#include <render/MeshRendererComponent.h>
#include <world/ITypedComponentStore.h>

#include <cstddef>
#include <cstdint>
#include <span>

//=============================================================================
// MeshRendererStore
//
// Component store for mesh renderers. When asset caches are supplied, stored
// handles are retained on add/replace and released on remove/destruction.
//=============================================================================
class MeshRendererStore final : public ITypedComponentStore<MeshRendererComponent>
{
public:
    MeshRendererStore() = default;
    MeshRendererStore(MeshCache& meshes, MaterialCache& materials);
    ~MeshRendererStore() override;

    MeshRendererStore(const MeshRendererStore&) = delete;
    MeshRendererStore& operator=(const MeshRendererStore&) = delete;
    MeshRendererStore(MeshRendererStore&&) = delete;
    MeshRendererStore& operator=(MeshRendererStore&&) = delete;

    void SetAssetCaches(MeshCache& meshes, MaterialCache& materials);

    bool Add(EntityId entity, const MeshRendererComponent& component) override;
    bool Remove(EntityId entity) override;

    [[nodiscard]] bool Contains(EntityId entity) const;
    [[nodiscard]] MeshRendererComponent* TryGet(EntityId entity) override;
    [[nodiscard]] const MeshRendererComponent* TryGet(EntityId entity) const override;
    [[nodiscard]] MeshRendererComponent* TryGetMutable(EntityId entity);

    [[nodiscard]] std::span<MeshRendererComponent> GetItems() override;
    [[nodiscard]] std::span<const MeshRendererComponent> GetItems() const override;
    [[nodiscard]] std::span<const EntityIndex> GetOwnerIds() const override;

    [[nodiscard]] size_t Count() const override;
    [[nodiscard]] bool IsEmpty() const override;
    [[nodiscard]] uint64_t GetVersion() const override;

private:
    void Attach(const MeshRendererComponent& component);
    void Detach(const MeshRendererComponent& component);

    MeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    SparseSet<MeshRendererComponent> Components;
};
