#include <render/StaticMeshComponentStore.h>

StaticMeshComponentStore::StaticMeshComponentStore(StaticMeshCache& meshes, MaterialCache& materials)
    : Meshes(&meshes)
    , Materials(&materials)
{
}

StaticMeshComponentStore::~StaticMeshComponentStore()
{
    for (const StaticMeshComponent& component : Components.GetItems())
        Detach(component);
}

void StaticMeshComponentStore::SetAssetCaches(StaticMeshCache& meshes, MaterialCache& materials)
{
    if (Meshes == &meshes && Materials == &materials)
        return;

    for (const StaticMeshComponent& component : Components.GetItems())
        Detach(component);

    Meshes = &meshes;
    Materials = &materials;

    for (const StaticMeshComponent& component : Components.GetItems())
        Attach(component);
}

bool StaticMeshComponentStore::Add(EntityId entity, const StaticMeshComponent& component)
{
    if (!entity.IsValid())
        return false;

    if (const StaticMeshComponent* existing = Components.TryGet(entity.Index))
        Detach(*existing);

    Attach(component);
    Components.Emplace(entity.Index, component);
    return true;
}

bool StaticMeshComponentStore::Remove(EntityId entity)
{
    if (!entity.IsValid())
        return false;

    if (const StaticMeshComponent* existing = Components.TryGet(entity.Index))
        Detach(*existing);

    return Components.Remove(entity.Index);
}

bool StaticMeshComponentStore::Contains(EntityId entity) const
{
    return entity.IsValid() && Components.Contains(entity.Index);
}

StaticMeshComponent* StaticMeshComponentStore::TryGet(EntityId entity)
{
    return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
}

const StaticMeshComponent* StaticMeshComponentStore::TryGet(EntityId entity) const
{
    return entity.IsValid() ? Components.TryGet(entity.Index) : nullptr;
}

StaticMeshComponent* StaticMeshComponentStore::TryGetMutable(EntityId entity)
{
    return TryGet(entity);
}

std::span<StaticMeshComponent> StaticMeshComponentStore::GetItems()
{
    auto& items = Components.GetItems();
    return { items.data(), items.size() };
}

std::span<const StaticMeshComponent> StaticMeshComponentStore::GetItems() const
{
    const auto& items = Components.GetItems();
    return { items.data(), items.size() };
}

std::span<const EntityIndex> StaticMeshComponentStore::GetOwnerIds() const
{
    const auto& owners = Components.GetOwners();
    return { owners.data(), owners.size() };
}

size_t StaticMeshComponentStore::Count() const
{
    return Components.Count();
}

bool StaticMeshComponentStore::IsEmpty() const
{
    return Components.IsEmpty();
}

uint64_t StaticMeshComponentStore::GetVersion() const
{
    return Components.GetVersion();
}

void StaticMeshComponentStore::Attach(const StaticMeshComponent& component)
{
    if (Meshes != nullptr)
        Meshes->Retain(component.Mesh);
    if (Materials != nullptr)
        Materials->Retain(component.Material);
}

void StaticMeshComponentStore::Detach(const StaticMeshComponent& component)
{
    if (Meshes != nullptr)
        Meshes->Release(component.Mesh);
    if (Materials != nullptr)
        Materials->Release(component.Material);
}
