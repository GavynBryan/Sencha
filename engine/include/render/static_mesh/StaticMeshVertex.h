#pragma once

#include <math/Vec.h>

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
