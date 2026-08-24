#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <render/MaterialSetCache.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// SkinnedMeshComponent
//
// ECS component that pairs an entity with a skinned mesh and its materials.
// Distinct from StaticMeshComponent by design (pipeline Decision J): the two
// kinds have different caches, loaders, and residency, and the type system is
// what keeps a skinned handle from being resolved through the static cache.
//
// Until the animation runtime lands there is no pose source, so extraction
// draws the rest geometry -- rest pose is bind pose, and every skinning matrix
// at bind pose is identity, so the rest draw is byte-exact with what a posed
// draw at bind pose will produce.
//
// Deliberately narrower than StaticMeshComponent, not incompletely mirrored:
// no baked-lighting fields because a skinned mesh is the canonical movable
// non-receiver (it gets no chart; direct light for it arrives with the baked-
// fallback light mode), no LayerMask because the camera-side counterpart does
// not exist, and no CastShadows-into-light-maps because that caster path does
// not read skinned components yet. Each field arrives with its reader,
// defaulted so scenes cooked before it keep loading -- a serialized field
// nothing consumes is how the alpha-mask defect shipped.
//
// Shadow participation is two independent facts, each read by exactly one
// system, never a combined mode: bake occlusion (AffectsBakedLighting, absent
// here -- a movable mesh must not bake its shadow in at the cook pose) and
// light-map casting (CastShadows, absent here until the map-caster path reads
// skinned geometry).
//=============================================================================
struct SkinnedMeshComponent
{
    SkinnedMeshHandle Mesh;
    MaterialSetHandle Materials;
    bool Visible = true;
    uint32_t SectionMask = 0xFFFFFFFFu;
    static_assert(kMaxMeshSections <= sizeof(decltype(SectionMask)) * 8,
                  "SectionMask must hold one bit per section that "
                  "ValidateMeshGeometry accepts, or extraction shifts past "
                  "its width");
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
