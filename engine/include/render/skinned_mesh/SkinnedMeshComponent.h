#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTypeId.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>
#include <assets/static_mesh/MeshGeometry.h>
#include <render/MaterialSetHandle.h>

#include <cstdint>

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
struct SENCHA_COMPONENT("SkinnedMesh")
       SENCHA_SCHEMA("SkinnedMesh")
       SENCHA_SCENE_CHUNK("SKIN")
SkinnedMeshComponent
{
    SENCHA_FIELD("mesh")
    SENCHA_ASSET(SkinnedMesh)
    SkinnedMeshHandle Mesh;

    SENCHA_FIELD("materials")
    SENCHA_ASSET_LIST(Material)
    MaterialSetHandle Materials;

    SENCHA_FIELD("visible")
    bool Visible = true;

    SENCHA_FIELD("section_mask")
    uint32_t SectionMask = 0xFFFFFFFFu;
    static_assert(kMaxMeshSections <= sizeof(decltype(SectionMask)) * 8,
                  "SectionMask must hold one bit per section that "
                  "ValidateMeshGeometry accepts, or extraction shifts past "
                  "its width");
};
SENCHA_COMPONENT_DECLARES_TRAITS(SkinnedMeshComponent);

#if !defined(SENCHA_CODEGEN)
#  include <render/skinned_mesh/SkinnedMeshComponent.sencha.h>
#endif
