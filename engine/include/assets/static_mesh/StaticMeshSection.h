#pragma once

#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>

struct StaticMeshSection
{
    uint32_t IndexOffset = 0;
    uint32_t IndexCount = 0;

    uint32_t VertexOffset = 0;
    uint32_t VertexCount = 0;

    uint32_t MaterialSlot = 0;

    Aabb3d LocalBounds = Aabb3d::Empty();
};
