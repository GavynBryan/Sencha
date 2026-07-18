#pragma once

#include <cstdint>
#include <vector>

#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

// A world-space triangle for the offline lighting bake.
struct BakeTriangle
{
    Vec3d V0;
    Vec3d V1;
    Vec3d V2;
};

// Median-split triangle BVH for offline bake visibility queries. Built once
// over a zone's (and halo's) world-space triangles, queried per (vertex,
// light) pair, then discarded. Cook-only, no physics dependency: the bake
// sees render geometry, not collision (renderer plan 7.2). Occlusion is a
// boolean over all triangles, so the result does not depend on build order.
class BakeBvh
{
public:
    void Build(std::vector<BakeTriangle> triangles);

    // True if the open segment (origin, target) is blocked by any triangle
    // strictly between the endpoints. The endpoints are excluded by a small
    // parametric epsilon so a ray lifted off a surface toward a light is not
    // reported as self-occluded, and the light itself is not a blocker.
    bool SegmentOccluded(const Vec3d& origin, const Vec3d& target) const;

    bool Empty() const { return Triangles.empty(); }

private:
    struct Node
    {
        Aabb3d Bounds = Aabb3d::Empty();
        std::uint32_t Start = 0;  // first entry in Order (leaf only)
        std::uint32_t Count = 0;  // triangle count (0 => internal node)
        std::uint32_t Left = 0;
        std::uint32_t Right = 0;
    };

    std::uint32_t BuildRange(std::uint32_t start, std::uint32_t count);

    std::vector<BakeTriangle> Triangles;
    std::vector<std::uint32_t> Order;  // triangle indices, partitioned per node
    std::vector<Node> Nodes;
};
