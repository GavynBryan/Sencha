#pragma once

#include <math/Vec.h>

// The base vertex shared by static and skinned meshes. Skinning influences
// are a *separate* stream (render/skinned_mesh/SkinnedMeshData.h), never
// interleaved here, so the static vertex layout is byte-identical whether or
// not a mesh is skinned (Decision M).
struct StaticMeshVertex
{
    Vec3d Position;
    Vec3d Normal;
    Vec2d Uv0;

    // xyz = tangent, w = handedness sign (+1/-1), the glTF convention:
    // bitangent = cross(Normal, Tangent.xyz) * Tangent.W. Generated at cook
    // (MikkTSpace) when the source lacks them (Decision M).
    Vec4 Tangent;
};
