#pragma once

#include <render/static_mesh/StaticMeshSection.h>
#include <render/static_mesh/StaticMeshVertex.h>

#include <vector>

struct StaticMeshData
{
    std::vector<StaticMeshVertex> Vertices;
    std::vector<uint32_t> Indices;
    Aabb3d LocalBounds = Aabb3d::Empty();
    std::vector<StaticMeshSection> Sections;
};
