#pragma once

#include <math/Vec.h>

#include <cstdint>
#include <vector>

// Backend-neutral mesh data. GPU upload, buffer ownership, and vertex input
// descriptions belong to graphics backends.
struct MeshVertex
{
    Vec3d Position = Vec3d::Zero();
    Vec3d Normal = Vec3d(0.0f, 1.0f, 0.0f);
    Vec4 Tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    Vec2d TexCoord = Vec2d::Zero();
};

struct MeshData
{
    std::vector<MeshVertex> Vertices;
    std::vector<uint32_t> Indices;

    [[nodiscard]] bool IsValid() const
    {
        return !Vertices.empty();
    }

    [[nodiscard]] bool IsIndexed() const
    {
        return !Indices.empty();
    }
};

