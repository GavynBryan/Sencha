#pragma once

#include <cstdint>

#include <math/Vec.h>

// The base vertex shared by static and skinned meshes. Skinning influences
// are a *separate* stream (assets/skinned_mesh/SkinnedMeshData.h), never
// interleaved here, so the static vertex layout is byte-identical whether or
// not a mesh is skinned (Decision M).
struct StaticMeshVertex
{
    Vec3d Position{};
    Vec3d Normal{};
    Vec2d Uv0{};

    // xyz = tangent, w = handedness sign (+1/-1), the glTF convention:
    // bitangent = cross(Normal, Tangent.xyz) * Tangent.W. Generated at cook
    // (MikkTSpace) when the source lacks them (Decision M).
    Vec4 Tangent{};

    // Lightmap UV, unorm16 per axis, read as VK_FORMAT_R16G16_UNORM. Cooked
    // cell meshes store absolute atlas texel-center coordinates; instanceable
    // meshes store a [0,1] sheet the per-instance scale/bias remaps into
    // their atlas rect. Zero is safe on unbaked meshes: with the identity
    // remap it lands on the atlas's reserved black border texel, and items
    // with no atlas skip the baked term entirely.
    std::uint16_t LightmapU = 0;
    std::uint16_t LightmapV = 0;
};
