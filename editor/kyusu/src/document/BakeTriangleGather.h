#pragma once

#include <assets/cook/BakeBvh.h>
#include <math/Mat.h>
#include <render/static_mesh/MeshGeometry.h>

#include <cstddef>
#include <vector>

// Appends `geometry`'s indexed triangles, transformed by `toWorld`, to `out`.
// Shared by the document cook's occlusion set and neighbor zone-halo collection
// so both occlude against the same world geometry.
inline void GatherWorldTriangles(const MeshGeometry& geometry, const Mat4& toWorld,
                                 std::vector<BakeTriangle>& out)
{
    for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
        out.push_back(BakeTriangle{
            toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i]].Position),
            toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i + 1]].Position),
            toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i + 2]].Position) });
}
