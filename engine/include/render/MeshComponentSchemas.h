#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <math/Vec.h>
#include <render/MaterialSetCache.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and mesh/material ownership for the drawn-mesh components.
//
// Both kinds hold the same pair of references and retain them the same way, so
// they change together; a light does not.
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
        // The shadow and bake fields default so scenes cooked before they
        // existed keep loading.
        const StaticMeshComponent defaults;
        return std::tuple{
            MakeField("mesh", &StaticMeshComponent::Mesh).AsAsset(AssetType::StaticMesh),
            MakeField("materials", &StaticMeshComponent::Materials)
                .AsAsset(AssetType::Material, AssetArity::List),
            MakeField("visible", &StaticMeshComponent::Visible),
            MakeField("cast_shadows", &StaticMeshComponent::CastShadows)
                .Default(defaults.CastShadows)
                .Label("Casts Shadows (shadow maps)")
                .Tooltip("This mesh renders into realtime shadow maps. "
                         "Unrelated to baked lighting."),
            MakeField("affects_baked_lighting", &StaticMeshComponent::AffectsBakedLighting)
                .Default(defaults.AffectsBakedLighting)
                .Label("Blocks Baked Light")
                .Tooltip("This mesh occludes light during the bake: it casts "
                         "shadows into lightmaps and probe bounce. Turn off "
                         "for anything that moves, or its shadow bakes in at "
                         "the cooked pose."),
            MakeField("layer_mask", &StaticMeshComponent::LayerMask),
            MakeField("section_mask", &StaticMeshComponent::SectionMask),
            MakeField("lightmap_scale_bias", &StaticMeshComponent::LightmapScaleBias)
                .Default(defaults.LightmapScaleBias),
        };
    }
};

struct SkinnedMeshComponentAssets
{
    SkinnedMeshComponentAssets() = default;
    SkinnedMeshComponentAssets(SkinnedMeshCache* meshes, MaterialSetCache* materialSets)
        : Meshes(meshes)
        , MaterialSets(materialSets)
    {
    }

    SkinnedMeshCache* Meshes = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
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

template <>
struct TypeSchema<SkinnedMeshComponent>
{
    static constexpr std::string_view Name = "SkinnedMesh";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'K', 'I', 'N');

    static auto Fields()
    {
        return std::tuple{
            MakeField("mesh", &SkinnedMeshComponent::Mesh).AsAsset(AssetType::SkinnedMesh),
            MakeField("materials", &SkinnedMeshComponent::Materials)
                .AsAsset(AssetType::Material, AssetArity::List),
            MakeField("visible", &SkinnedMeshComponent::Visible),
            MakeField("section_mask", &SkinnedMeshComponent::SectionMask),
        };
    }
};
