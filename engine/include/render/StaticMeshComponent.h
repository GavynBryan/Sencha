#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <render/Material.h>
#include <render/MaterialSetCache.h>
#include <assets/static_mesh/MeshGeometry.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// StaticMeshComponent
//
// ECS component that pairs an entity with a mesh and material. SectionMask is
// a bitmask read by extraction: a cleared bit skips that section.
//=============================================================================
struct StaticMeshComponent
{
    StaticMeshHandle Mesh;
    MaterialSetHandle Materials;
    bool Visible = true;
    bool CastShadows = true;
    bool AffectsBakedLighting = true;

    // Which view layers this mesh belongs to. Authored and serialized, and
    // deliberately unread today: the mask selects against a camera-side
    // counterpart that does not exist yet, so adding one is a cooked-scene
    // format change and needs a `.Default()`.
    //
    // Kept rather than deleted because its consumer is named: a first-person
    // view model has to render with its own projection and depth clear so the
    // weapon neither clips into geometry nor shares the world's field of view,
    // and the mask is how geometry is selected into that pass. Roadmap Track B
    // parks view models with the remaining scene effects.
    //
    // Do not wire half of it. A reader without an authoring surface is a
    // silently-ignored field, which is what this comment exists to stop the
    // next person rediscovering.
    uint32_t LayerMask = 0xFFFFFFFFu;

    uint32_t SectionMask = 0xFFFFFFFFu;
    static_assert(kMaxMeshSections <= sizeof(decltype(SectionMask)) * 8,
                  "SectionMask must hold one bit per section that "
                  "ValidateMeshGeometry accepts, or extraction shifts past "
                  "its width");
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

// Stated rather than derived from TypeSchema::Name, so the schema can move
// without the identity moving with it. The name is repeated exactly.
SENCHA_DECLARE_COMPONENT_TYPE(StaticMeshComponent, "StaticMesh");
