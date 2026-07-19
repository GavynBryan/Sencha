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
    MaterialSetHandle Materials;
    bool Visible = true;
    bool CastShadows = true;
    bool AffectsBakedLighting = true;
    uint32_t LayerMask = 0xFFFFFFFFu;
    uint32_t SectionMask = 0xFFFFFFFFu;
    // Remaps the mesh's lightmap UVs into this placement's atlas rect
    // (uv * xy + zw). Identity for cooked cell meshes (absolute atlas UVs);
    // the cook assigns per-placement rects to instanceable meshes.
    Vec4 LightmapScaleBias = Vec4{ 1.0f, 1.0f, 0.0f, 0.0f };
};

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
                .Default(defaults.CastShadows),
            MakeField("affects_baked_lighting", &StaticMeshComponent::AffectsBakedLighting)
                .Default(defaults.AffectsBakedLighting),
            MakeField("layer_mask", &StaticMeshComponent::LayerMask),
            MakeField("section_mask", &StaticMeshComponent::SectionMask),
            MakeField("lightmap_scale_bias", &StaticMeshComponent::LightmapScaleBias)
                .Default(defaults.LightmapScaleBias),
        };
    }
};
