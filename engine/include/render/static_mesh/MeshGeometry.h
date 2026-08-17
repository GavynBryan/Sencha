#pragma once

#include <render/static_mesh/StaticMeshSection.h>
#include <render/static_mesh/StaticMeshVertex.h>

#include <cstddef>
#include <vector>

//=============================================================================
// MeshGeometry
//
// The shared geometry core every renderable mesh is built on: a vertex
// stream (position/normal/uv/tangent — StaticMeshVertex), an index stream,
// local bounds, and material-slotted sections. Static meshes ARE this;
// skinned meshes embed it and add a skinning stream + skeleton reference
// (SkinnedMeshData). Keeping the geometry a distinct shared type is what
// lets the static and skinned paths reuse one cook, one serializer core,
// and one GPU upload without conflating their asset types or runtimes.
//=============================================================================

// Sections are selected per entity by a 32-bit bitmask
// (StaticMeshComponent::SectionMask), and extraction shifts by the section
// index to test it. A 33rd section would shift past the width of that mask,
// which is undefined behavior, so the cap is a validation error rather than
// a runtime surprise.
inline constexpr std::size_t kMaxMeshSections = 32;

struct MeshGeometry
{
    std::vector<StaticMeshVertex> Vertices;
    std::vector<uint32_t> Indices;
    Aabb3d LocalBounds = Aabb3d::Empty();
    std::vector<StaticMeshSection> Sections;
};

// Whether any vertex carries a non-zero lightmap UV, which is what decides
// whether an instance may sample its zone's baked atlas.
//
// Derived from the vertices rather than read from the .smesh header flag: the
// flag is a tooling hint the loader does not propagate, and geometry built in
// memory (preview primitives, cooked brush cells) never passes through a
// header. Deriving it covers every source with one rule.
//
// A mesh whose lightmap UVs are all exactly zero is indistinguishable from one
// that was never unwrapped, and that is the intended reading: origin-only UVs
// carry no bake.
[[nodiscard]] inline bool GeometryHasLightmapUvs(const MeshGeometry& geometry)
{
    for (const StaticMeshVertex& vertex : geometry.Vertices)
        if (vertex.LightmapU != 0 || vertex.LightmapV != 0)
            return true;
    return false;
}
