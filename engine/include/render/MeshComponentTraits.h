#pragma once

#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <render/MaterialSetCache.h>
#include <render/MeshComponentAssets.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>

//=============================================================================
// Mesh and material ownership for the drawn-mesh components.
//
// Both kinds hold the same pair of references and retain them the same way, so
// they change together; a light does not.
//=============================================================================

template <>
struct ComponentTraits<StaticMeshComponent>
{
    static void OnAdd(StaticMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->Meshes != nullptr)
            assets->Meshes->Retain(component.Mesh);
        if (assets->MaterialSets != nullptr)
            assets->MaterialSets->Retain(component.Materials);
    }

    static void OnRemove(const StaticMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->MaterialSets != nullptr)
            assets->MaterialSets->Release(component.Materials);
        if (assets->Meshes != nullptr)
            assets->Meshes->Release(component.Mesh);
    }
};

template <>
struct ComponentTraits<SkinnedMeshComponent>
{
    static void OnAdd(SkinnedMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<SkinnedMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->Meshes != nullptr)
            assets->Meshes->Retain(component.Mesh);
        if (assets->MaterialSets != nullptr)
            assets->MaterialSets->Retain(component.Materials);
    }

    static void OnRemove(const SkinnedMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<SkinnedMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->MaterialSets != nullptr)
            assets->MaterialSets->Release(component.Materials);
        if (assets->Meshes != nullptr)
            assets->Meshes->Release(component.Mesh);
    }
};
