#pragma once

#include <anim/SkeletonHandle.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <math/Vec.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/MaterialSetCache.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/TextureHandle.h>
#include <render/ZoneLightmapComponent.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cstdint>
#include <string_view>
#include <tuple>

class TextureCache;

//=============================================================================
// What the render components mean to the code that authors and owns them
//
// Authoring shape and asset lifetime for every render component, in the place
// registration and the serializers look. A system that draws a mesh includes
// the component and gets a mesh handle; it does not get the mesh cache, the
// material set cache, or the World.
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

struct ZoneLightmapComponentAssets
{
    ZoneLightmapComponentAssets() = default;
    explicit ZoneLightmapComponentAssets(TextureCache* textures)
        : Textures(textures)
    {
    }

    TextureCache* Textures = nullptr;
};

// Defined in ZoneLightmapComponent.cpp. Retaining a texture needs the cache's
// definition, and this header is pulled in by scene serialization, schema
// startup, and the editor's queue builder -- none of which should acquire a
// dependency on the Vulkan-side texture cache to name a component. Sibling
// components can inline their hooks because their caches (StaticMeshCache,
// MaterialSetCache) live under render/; TextureCache does not.
template <>
struct ComponentTraits<ZoneLightmapComponent>
{
    static void OnAdd(ZoneLightmapComponent& component, World& world, EntityId);
    static void OnRemove(const ZoneLightmapComponent& component, World& world,
                         EntityId);
};

template <>
struct TypeSchema<ZoneLightmapComponent>
{
    static constexpr std::string_view Name = "ZoneLightmap";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('Z', 'L', 'M', 'P');

    static auto Fields()
    {
        return std::tuple{
            MakeField("texture", &ZoneLightmapComponent::Texture)
                .AsAsset(AssetType::Texture),
            MakeField("ao", &ZoneLightmapComponent::Ao)
                .AsAsset(AssetType::Texture)
                .Default(TextureHandle{}),
        };
    }
};

template <>
struct TypeSchema<PointLightComponent>
{
    static constexpr std::string_view Name = "PointLight";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'L', 'G', 'T');

    static auto Fields()
    {
        const PointLightComponent defaults;
        return std::tuple{
            MakeField("color", &PointLightComponent::Color)
                .AsColor()
                .Default(defaults.Color),
            MakeField("intensity", &PointLightComponent::Intensity)
                .Default(defaults.Intensity),
            MakeField("range", &PointLightComponent::Range)
                .Default(defaults.Range),
            MakeField("enabled", &PointLightComponent::Enabled)
                .Default(defaults.Enabled),
            MakeField("cast_shadows", &PointLightComponent::CastShadows)
                .Default(defaults.CastShadows)
                .Tooltip("Renders a realtime shadow map for this light. "
                         "Costs one of the frame's budgeted shadow slots."),
            MakeField("shadow_resolution", &PointLightComponent::ShadowResolution)
                .Default(defaults.ShadowResolution)
                .Tooltip("Requested shadow-map tile size: Low 256, Medium "
                         "512, High 1024. Spot lights only -- point-light "
                         "cube faces are fixed at 512."),
            MakeField("shadow_update", &PointLightComponent::ShadowUpdate)
                .Default(defaults.ShadowUpdate)
                .Label("Shadow Update")
                .Tooltip("How often this light's shadow map re-renders. "
                         "Unrelated to baked lighting."),
            MakeField("shadow_softness", &PointLightComponent::ShadowSoftness)
                .Default(defaults.ShadowSoftness)
                .Tooltip("Widens the shadow filter, in texels, on top of the "
                         "global softness setting."),
            MakeField("shadow_bias_scale", &PointLightComponent::ShadowBiasScale)
                .Default(defaults.ShadowBiasScale)
                .Tooltip("Scales the depth bias that stops a surface "
                         "shadowing itself. Raise if lit surfaces stripe; "
                         "lower if shadows detach from their objects."),
            MakeField("bake_contribution", &PointLightComponent::BakeContribution)
                .Default(defaults.BakeContribution)
                .Label("Lighting")
                .Tooltip("How this light participates in baked lighting. "
                         "Baking happens when the zone cooks."),
        };
    }
};

template <>
struct TypeSchema<SpotLightComponent>
{
    static constexpr std::string_view Name = "SpotLight";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'P', 'O', 'T');

    static auto Fields()
    {
        const SpotLightComponent defaults;
        return std::tuple{
            MakeField("color", &SpotLightComponent::Color)
                .AsColor()
                .Default(defaults.Color),
            MakeField("intensity", &SpotLightComponent::Intensity)
                .Default(defaults.Intensity),
            MakeField("range", &SpotLightComponent::Range)
                .Default(defaults.Range),
            MakeField("inner_angle_degrees", &SpotLightComponent::InnerAngleDegrees)
                .Default(defaults.InnerAngleDegrees),
            MakeField("outer_angle_degrees", &SpotLightComponent::OuterAngleDegrees)
                .Default(defaults.OuterAngleDegrees),
            MakeField("enabled", &SpotLightComponent::Enabled)
                .Default(defaults.Enabled),
            MakeField("cast_shadows", &SpotLightComponent::CastShadows)
                .Default(defaults.CastShadows)
                .Tooltip("Renders a realtime shadow map for this light. "
                         "Costs one of the frame's budgeted shadow slots."),
            MakeField("shadow_resolution", &SpotLightComponent::ShadowResolution)
                .Default(defaults.ShadowResolution)
                .Tooltip("Requested shadow-map tile size: Low 256, Medium "
                         "512, High 1024."),
            MakeField("shadow_update", &SpotLightComponent::ShadowUpdate)
                .Default(defaults.ShadowUpdate)
                .Label("Shadow Update")
                .Tooltip("How often this light's shadow map re-renders. "
                         "Unrelated to baked lighting."),
            MakeField("shadow_softness", &SpotLightComponent::ShadowSoftness)
                .Default(defaults.ShadowSoftness)
                .Tooltip("Widens the shadow filter, in texels, on top of the "
                         "global softness setting."),
            MakeField("shadow_bias_scale", &SpotLightComponent::ShadowBiasScale)
                .Default(defaults.ShadowBiasScale)
                .Tooltip("Scales the depth bias that stops a surface "
                         "shadowing itself. Raise if lit surfaces stripe; "
                         "lower if shadows detach from their objects."),
            MakeField("bake_contribution", &SpotLightComponent::BakeContribution)
                .Default(defaults.BakeContribution)
                .Label("Lighting")
                .Tooltip("How this light participates in baked lighting. "
                         "Baking happens when the zone cooks."),
        };
    }
};

template <>
struct TypeSchema<IrradianceVolumeComponent>
{
    static constexpr std::string_view Name = "IrradianceVolume";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('I', 'R', 'V', 'L');

    static auto Fields()
    {
        const IrradianceVolumeComponent defaults{};
        return std::tuple{
            MakeField("half_extents", &IrradianceVolumeComponent::HalfExtents)
                .Default(defaults.HalfExtents),
            MakeField("cell_size", &IrradianceVolumeComponent::CellSize)
                .Default(defaults.CellSize),
            MakeField("priority", &IrradianceVolumeComponent::Priority)
                .Default(defaults.Priority),
        };
    }
};
