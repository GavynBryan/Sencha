#pragma once

#include <render/static_mesh/StaticMeshSection.h>
#include <render/static_mesh/StaticMeshVertex.h>

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
struct MeshGeometry
{
    std::vector<StaticMeshVertex> Vertices;
    std::vector<uint32_t> Indices;
    Aabb3d LocalBounds = Aabb3d::Empty();
    std::vector<StaticMeshSection> Sections;
};
