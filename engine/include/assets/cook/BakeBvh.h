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

// Nearest intersection along a ray. Normal is the unit geometric normal from
// the triangle's winding (not flipped toward the ray); Backface reports
// whether the ray struck the winding's far side.
struct BakeBvhHit
{
    double T = 0.0;
    Vec3d Position;
    Vec3d Normal;
    bool Backface = false;
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

    // True when the NEAREST triangle hit along origin + t*direction (unit
    // length not required; t in (max(eps, minT), maxT]) faces away from the
    // ray: seeing a backface first means the point sits inside or underneath
    // solid geometry. The bake uses this to invalidate buried lightmap
    // samples (e.g. floor luxels underneath an overlapping wall brush) so
    // dilation fills them from lit neighbors instead of baking black that
    // bilinear filtering would drag out past the wall base. minT lets a
    // reverse probe skip past the sample's own surface and any flush
    // partners coplanar with it. False on a miss.
    bool FirstHitIsBackface(const Vec3d& origin, const Vec3d& direction,
                            double maxT, double minT = 0.0) const;

    // Nearest triangle hit along origin + t*direction (unit length not
    // required; t in (max(eps, minT), maxT]). False on a miss. The probe
    // bake gathers bounce radiance at the hit and classifies buried probes
    // with it.
    bool FirstHit(const Vec3d& origin, const Vec3d& direction, double maxT,
                  BakeBvhHit& hit, double minT = 0.0) const;

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
