#include <render/MeshRendererStore.h>

MeshRendererStore::MeshRendererStore(StaticMeshCache& meshes, MaterialCache& materials)
    : Meshes(&meshes)
    , Materials(&materials)
{
}

MeshRendererStore::~MeshRendererStore()
{
    for (const MeshRendererComponent& component : Components.GetItems())
        Detach(component);
}

void MeshRendererStore::SetAssetCaches(StaticMeshCache& meshes, MaterialCache& materials)
{
    if (Meshes == &meshes && Materials == &materials)
        return;

    for (const MeshRendererComponent& component : Components.GetItems())
        Detach(component);

    Meshes = &meshes;
    Materials = &materials;

    for (const MeshRendererComponent& component : Components.GetItems())
        Attach(component);
}

bool MeshRendererStore::Add(EntityId entity, const MeshRendererComponent& component)
{
    if (!entity.IsValid())
        return false;

    if (const MeshRendererComponent* existing = Components.TryGet(entity.Index))
        Detach(*existing);

    Attach(component);
    Components.Emplace(entity.Index, component);
    return true;
}

bool MeshRendererStore::Remove(EntityId entity)
{
    if (!entity.IsValid())
        return false;

    if (const MeshRendererComponent* existing = Components.TryGet(entity.Index))
        Detach(*existing);

    return Components.Remove(entity.Index);
}

bool MeshRendererStore::Contains(EntityId entity) const
{
    return entity.IsValid() && Components.Contains(entity.Index);
}

MeshRendererComponent* MeshRendererStore::TryGet(EntityId entity)
{
    return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
}

const MeshRendererComponent* MeshRendererStore::TryGet(EntityId entity) const
{
    return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
}

MeshRendererComponent* MeshRendererStore::TryGetMutable(EntityId entity)
{
    return TryGet(entity);
}

std::span<MeshRendererComponent> MeshRendererStore::GetItems()
{
    auto& items = Components.GetItems();
    return { items.data(), items.size() };
}

std::span<const MeshRendererComponent> MeshRendererStore::GetItems() const
{
    const auto& items = Components.GetItems();
    return { items.data(), items.size() };
}

std::span<const EntityIndex> MeshRendererStore::GetOwnerIds() const
{
    const auto& owners = Components.GetOwners();
    return { owners.data(), owners.size() };
}

size_t MeshRendererStore::Count() const
{
    return Components.Count();
}

bool MeshRendererStore::IsEmpty() const
{
    return Components.IsEmpty();
}

uint64_t MeshRendererStore::GetVersion() const
{
    return Components.GetVersion();
}

void MeshRendererStore::Attach(const MeshRendererComponent& component)
{
    if (Meshes != nullptr)
        Meshes->Retain(component.Mesh);
    if (Materials != nullptr)
        Materials->Retain(component.Material);
}

void MeshRendererStore::Detach(const MeshRendererComponent& component)
{
    if (Meshes != nullptr)
        Meshes->Release(component.Mesh);
    if (Materials != nullptr)
        Materials->Release(component.Material);
}
