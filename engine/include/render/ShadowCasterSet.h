#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <vector>

struct ShadowCasterItem
{
    StaticMeshHandle Mesh;
    MaterialHandle Material;
    std::uint32_t SectionIndex = 0;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    bool DoubleSided = false;
};

struct ShadowCasterSet
{
    std::vector<ShadowCasterItem> Items;

    void Reset() { Items.clear(); }
};
