#pragma once

#include <ecs/ComponentTypeId.h>
#include <render/Material.h>
#include <assets/static_mesh/MeshGeometry.h>
#include <render/MaterialSetHandle.h>
#include <render/static_mesh/StaticMeshHandle.h>

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

SENCHA_DECLARE_COMPONENT_TYPE(StaticMeshComponent, "StaticMesh");
