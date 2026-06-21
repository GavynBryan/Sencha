#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/World.h>
#include <render/Material.h>
#include <render/MaterialSetCache.h>
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
    // Per-section material binding, instance-level: the mesh's sections index
    // this set by MaterialSlot (a section beyond the set falls back to the last
    // member). One handle keeps the component trivially copyable; the variable-
    // length list lives in MaterialSetCache.
    MaterialSetHandle Materials;
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
    StaticMeshComponentAssets(StaticMeshCache* meshes, MaterialSetCache* materialSets)
        : Meshes(meshes)
        , MaterialSets(materialSets)
    {
    }

    StaticMeshCache* Meshes = nullptr;
    // The set owns references to its member materials, so retaining the set is
    // all the component lifecycle needs to do for material lifetime.
    MaterialSetCache* MaterialSets = nullptr;
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
struct TypeSchema<StaticMeshComponent>
{
    static constexpr std::string_view Name = "StaticMesh";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('M', 'E', 'S', 'H');

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &StaticMeshComponent::Mesh).AsAsset(AssetType::StaticMesh),
            MakeField("materials", &StaticMeshComponent::Materials)
                .AsAsset(AssetType::Material, AssetArity::List),
            MakeField("visible", &StaticMeshComponent::Visible),
            MakeField("layer_mask", &StaticMeshComponent::LayerMask),
            MakeField("section_mask", &StaticMeshComponent::SectionMask),
        };
    }
};
