#pragma once

#include <cstdint>

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

    // Baked static direct diffuse, RGBM-packed (R8G8B8A8): the summed
    // contribution of lights authored LightBakeContribution::Direct, computed
    // per vertex by the lighting bake. Zero is neutral (unbaked meshes add
    // nothing). Read as VK_FORMAT_R8G8B8A8_UNORM and decoded rgb * a * range
    // in the forward vertex shader.
    std::uint32_t BakedDirect = 0;
};
