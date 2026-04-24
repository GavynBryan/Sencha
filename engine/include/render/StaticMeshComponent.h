#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTraits.h>
#include <ecs/World.h>
#include <render/Material.h>
#include <render/MaterialCache.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// StaticMeshComponent
//
// ECS component that pairs an entity with a mesh and material. LayerMask and
// SectionMask are bitmasks: a cleared bit skips the corresponding layer or
// section during extraction.
//=============================================================================
struct StaticMeshComponent
{
    StaticMeshHandle Mesh;
    MaterialHandle Material;
    bool Visible = true;
    uint32_t LayerMask = 0xFFFFFFFFu;
    uint32_t SectionMask = 0xFFFFFFFFu;
};

//=============================================================================
// StaticMeshComponentAssets
//
// Registry-local asset-cache pointers used by StaticMeshComponent lifecycle
// hooks. A zone without render asset caches may leave either pointer null.
//=============================================================================
struct StaticMeshComponentAssets
{
    StaticMeshComponentAssets() = default;
    StaticMeshComponentAssets(StaticMeshCache* meshes, MaterialCache* materials)
        : Meshes(meshes)
        , Materials(materials)
    {
    }

    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
};

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
        if (assets->Materials != nullptr)
            assets->Materials->Retain(component.Material);
    }

    static void OnRemove(const StaticMeshComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<StaticMeshComponentAssets>();
        if (assets == nullptr)
            return;

        if (assets->Materials != nullptr)
            assets->Materials->Release(component.Material);
        if (assets->Meshes != nullptr)
            assets->Meshes->Release(component.Mesh);
    }
};

template <>
struct TypeSchema<StaticMeshComponent>
{
    static constexpr std::string_view Name = "StaticMesh";

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &StaticMeshComponent::Mesh),
            MakeField("material", &StaticMeshComponent::Material),
            MakeField("visible", &StaticMeshComponent::Visible),
            MakeField("layer_mask", &StaticMeshComponent::LayerMask),
            MakeField("section_mask", &StaticMeshComponent::SectionMask),
        };
    }
};
