#include <render/StaticMeshComponent.h>

#include <ecs/World.h>
#include <render/MaterialSetCache.h>
#include <render/MeshComponentAssets.h>
#include <render/static_mesh/StaticMeshCache.h>

void ComponentTraits<StaticMeshComponent>::OnAdd(
    StaticMeshComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
    if (assets == nullptr)
        return;

    if (assets->Meshes != nullptr)
        assets->Meshes->Retain(component.Mesh);
    if (assets->MaterialSets != nullptr)
        assets->MaterialSets->Retain(component.Materials);
}

void ComponentTraits<StaticMeshComponent>::OnRemove(
    const StaticMeshComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
    if (assets == nullptr)
        return;

    if (assets->MaterialSets != nullptr)
        assets->MaterialSets->Release(component.Materials);
    if (assets->Meshes != nullptr)
        assets->Meshes->Release(component.Mesh);
}
