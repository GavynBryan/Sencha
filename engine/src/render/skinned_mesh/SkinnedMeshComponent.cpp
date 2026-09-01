#include <render/skinned_mesh/SkinnedMeshComponent.h>

#include <ecs/World.h>
#include <render/MaterialSetCache.h>
#include <render/MeshComponentAssets.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>

void ComponentTraits<SkinnedMeshComponent>::OnAdd(
    SkinnedMeshComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<SkinnedMeshComponentAssets>();
    if (assets == nullptr)
        return;

    if (assets->Meshes != nullptr)
        assets->Meshes->Retain(component.Mesh);
    if (assets->MaterialSets != nullptr)
        assets->MaterialSets->Retain(component.Materials);
}

void ComponentTraits<SkinnedMeshComponent>::OnRemove(
    const SkinnedMeshComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<SkinnedMeshComponentAssets>();
    if (assets == nullptr)
        return;

    if (assets->MaterialSets != nullptr)
        assets->MaterialSets->Release(component.Materials);
    if (assets->Meshes != nullptr)
        assets->Meshes->Release(component.Mesh);
}
