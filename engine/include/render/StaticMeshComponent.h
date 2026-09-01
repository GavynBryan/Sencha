#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <render/Material.h>
#include <assets/static_mesh/MeshGeometry.h>
#include <render/MaterialSetHandle.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>

//=============================================================================
// StaticMeshComponent
//
// ECS component that pairs an entity with a mesh and material. SectionMask is
// a bitmask read by extraction: a cleared bit skips that section.
//=============================================================================
struct SENCHA_COMPONENT("StaticMesh")
       SENCHA_SCHEMA("StaticMesh")
       SENCHA_SCENE_CHUNK("MESH")
StaticMeshComponent
{
    SENCHA_FIELD("mesh")
    SENCHA_ASSET(StaticMesh)
    StaticMeshHandle Mesh;

    SENCHA_FIELD("materials")
    SENCHA_ASSET_LIST(Material)
    MaterialSetHandle Materials;

    SENCHA_FIELD("visible")
    bool Visible = true;

    SENCHA_FIELD("cast_shadows")
    SENCHA_LABEL("Casts Shadows (shadow maps)")
    SENCHA_TOOLTIP("This mesh renders into realtime shadow maps. Unrelated to "
                   "baked lighting.")
    bool CastShadows = true;

    SENCHA_FIELD("affects_baked_lighting")
    SENCHA_LABEL("Blocks Baked Light")
    SENCHA_TOOLTIP("This mesh occludes light during the bake: it casts shadows "
                   "into lightmaps and probe bounce. Turn off for anything that "
                   "moves, or its shadow bakes in at the cooked pose.")
    bool AffectsBakedLighting = true;

    // Which view layers this mesh belongs to. Authored and serialized, and
    // deliberately unread today: the mask selects against a camera-side
    // counterpart that does not exist yet.
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
    SENCHA_FIELD("layer_mask")
    uint32_t LayerMask = 0xFFFFFFFFu;

    SENCHA_FIELD("section_mask")
    uint32_t SectionMask = 0xFFFFFFFFu;
    static_assert(kMaxMeshSections <= sizeof(decltype(SectionMask)) * 8,
                  "SectionMask must hold one bit per section that "
                  "ValidateMeshGeometry accepts, or extraction shifts past "
                  "its width");

    // Remaps the mesh's lightmap UVs into this placement's atlas rect
    // (uv * xy + zw). Identity for cooked cell meshes (absolute atlas UVs);
    // the cook assigns per-placement rects to instanceable meshes.
    SENCHA_FIELD("lightmap_scale_bias")
    Vec4 LightmapScaleBias = Vec4{ 1.0f, 1.0f, 0.0f, 0.0f };
};

// Holds the mesh and material set for as long as the component is carried,
// through the StaticMeshComponentAssets resource the host points at its caches.
template <>
struct ComponentTraits<StaticMeshComponent>
{
    static void OnAdd(StaticMeshComponent& component, World& world, EntityId);
    static void OnRemove(const StaticMeshComponent& component, World& world, EntityId);
};

#if !defined(SENCHA_CODEGEN)
#  include <render/StaticMeshComponent.sencha.h>
#endif
