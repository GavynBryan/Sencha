#include <assets/cook/BakeBvh.h>

#include <algorithm>
#include <array>

namespace
{
    constexpr std::uint32_t kLeafTriangleCount = 4;

    Aabb3d TriangleBounds(const BakeTriangle& tri)
    {
        Aabb3d bounds = Aabb3d::Empty();
        bounds.ExpandToInclude(tri.V0);
        bounds.ExpandToInclude(tri.V1);
        bounds.ExpandToInclude(tri.V2);
        return bounds;
    }

    Vec3d TriangleCentroid(const BakeTriangle& tri)
    {
        return (tri.V0 + tri.V1 + tri.V2) / 3.0f;
    }

    // Slab test: does the segment origin + t*dir, t in [tMin, tMax], touch the
    // box? Uses the reciprocal direction; a zero component is handled by the
    // sign-agnostic min/max on infinities.
    bool SegmentIntersectsBox(const Aabb3d& box, const Vec3d& origin,
                              const Vec3d& invDir, double tMin, double tMax)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            double t0 = (box.Min[axis] - origin[axis]) * invDir[axis];
            double t1 = (box.Max[axis] - origin[axis]) * invDir[axis];
            if (t0 > t1)
                std::swap(t0, t1);
            tMin = t0 > tMin ? t0 : tMin;
            tMax = t1 < tMax ? t1 : tMax;
            if (tMin > tMax)
                return false;
        }
        return true;
    }

    struct DVec
    {
        double X, Y, Z;
    };

    DVec ToDouble(const Vec3d& v) { return { v.X, v.Y, v.Z }; }
    DVec Sub(const DVec& a, const DVec& b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
    DVec Cross(const DVec& a, const DVec& b)
    {
        return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
    }
    double Dot(const DVec& a, const DVec& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

    // Moller-Trumbore, entirely in double: float cross products near a shared
    // triangle edge can put the barycentrics just outside [0, 1] for BOTH
    // triangles, opening one-texel light pinholes through solid walls where a
    // shadow segment grazes the seam. Returns the ray parameter t of the hit
    // (origin + t*dir), or a negative value on miss / degenerate tri.
    double RayTriangleT(const Vec3d& origin, const Vec3d& dir,
                        const BakeTriangle& tri)
    {
        constexpr double kParallelEps = 1e-12;
        const DVec v0 = ToDouble(tri.V0);
        const DVec edge1 = Sub(ToDouble(tri.V1), v0);
        const DVec edge2 = Sub(ToDouble(tri.V2), v0);
        const DVec pvec = Cross(ToDouble(dir), edge2);
        const double det = Dot(edge1, pvec);
        if (det > -kParallelEps && det < kParallelEps)
            return -1.0;
        const double invDet = 1.0 / det;
        const DVec tvec = Sub(ToDouble(origin), v0);
        const double u = Dot(tvec, pvec) * invDet;
        if (u < 0.0 || u > 1.0)
            return -1.0;
        const DVec qvec = Cross(tvec, edge1);
        const double v = Dot(ToDouble(dir), qvec) * invDet;
        if (v < 0.0 || u + v > 1.0)
            return -1.0;
        return Dot(edge2, qvec) * invDet;
    }
}

void BakeBvh::Build(std::vector<BakeTriangle> triangles)
{
    Triangles = std::move(triangles);
    Nodes.clear();
    Order.resize(Triangles.size());
    for (std::uint32_t i = 0; i < Order.size(); ++i)
        Order[i] = i;
    if (Triangles.empty())
        return;
    Nodes.reserve(Triangles.size() * 2);
    BuildRange(0, static_cast<std::uint32_t>(Order.size()));
}

std::uint32_t BakeBvh::BuildRange(std::uint32_t start, std::uint32_t count)
{
    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(Nodes.size());
    Nodes.push_back(Node{});

    Aabb3d bounds = Aabb3d::Empty();
    for (std::uint32_t i = 0; i < count; ++i)
        bounds.ExpandToInclude(TriangleBounds(Triangles[Order[start + i]]));

    if (count <= kLeafTriangleCount)
    {
        Nodes[nodeIndex].Bounds = bounds;
        Nodes[nodeIndex].Start = start;
        Nodes[nodeIndex].Count = count;
        return nodeIndex;
    }

    // Split along the widest centroid axis at the median.
    const Vec3d extent = bounds.Extent();
    int axis = 0;
    if (extent.Y > extent.X)
        axis = 1;
    if (extent[2] > extent[axis])
        axis = 2;

    const std::uint32_t mid = count / 2;
    std::nth_element(
        Order.begin() + start, Order.begin() + start + mid,
        Order.begin() + start + count,
        [&](std::uint32_t a, std::uint32_t b) {
            return TriangleCentroid(Triangles[a])[axis]
                 < TriangleCentroid(Triangles[b])[axis];
        });

    const std::uint32_t left = BuildRange(start, mid);
    const std::uint32_t right = BuildRange(start + mid, count - mid);
    Nodes[nodeIndex].Bounds = bounds;
    Nodes[nodeIndex].Left = left;
    Nodes[nodeIndex].Right = right;
    return nodeIndex;
}

bool BakeBvh::FirstHit(const Vec3d& origin, const Vec3d& direction,
                       double maxT, BakeBvhHit& hit, double minT) const
{
    if (Nodes.empty())
        return false;

    constexpr double kBaseEps = 1e-4;
    const double kEps = minT > kBaseEps ? minT : kBaseEps;
    Vec3d invDir;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double d = direction[axis];
        invDir[axis] = static_cast<float>(
            (d > 0.0 || d < 0.0) ? 1.0 / d : 1e30);
    }

    double nearestT = maxT;
    const BakeTriangle* nearest = nullptr;
    bool nearestBackface = false;

    // Flush brush faces produce coincident triangle pairs with opposite
    // windings; a ray crossing that interface hits both at the same t and the
    // winner would otherwise be traversal order. Within this tie window the
    // front face wins, so an open-space ray never reports backface-first at a
    // kissing surface, and the answer is independent of build order.
    const auto tieWindow = [](double t) { return 1e-6 * (1.0 + t); };

    std::array<std::uint32_t, 64> stack{};
    std::uint32_t depth = 0;
    stack[depth++] = 0;
    while (depth > 0)
    {
        const Node& node = Nodes[stack[--depth]];
        if (!SegmentIntersectsBox(node.Bounds, origin, invDir, kEps,
                                  nearestT + tieWindow(nearestT)))
            continue;

        if (node.Count > 0)
        {
            for (std::uint32_t i = 0; i < node.Count; ++i)
            {
                const BakeTriangle& tri = Triangles[Order[node.Start + i]];
                const double t = RayTriangleT(origin, direction, tri);
                if (t <= kEps || t > nearestT + tieWindow(nearestT))
                    continue;
                const Vec3d triNormal =
                    (tri.V1 - tri.V0).Cross(tri.V2 - tri.V0);
                const bool backface = triNormal.Dot(direction) > 0.0f;
                if (nearest == nullptr || t < nearestT - tieWindow(nearestT))
                {
                    nearestT = t;
                    nearest = &tri;
                    nearestBackface = backface;
                }
                else if (nearestBackface && !backface)
                {
                    nearestT = t < nearestT ? t : nearestT;
                    nearest = &tri;
                    nearestBackface = false;
                }
            }
            continue;
        }

        if (depth + 2 <= stack.size())
        {
            stack[depth++] = node.Left;
            stack[depth++] = node.Right;
        }
    }

    if (nearest == nullptr)
        return false;

    const Vec3d normal =
        (nearest->V1 - nearest->V0).Cross(nearest->V2 - nearest->V0);
    const float length = normal.Magnitude();
    hit.T = nearestT;
    hit.Position = origin + direction * static_cast<float>(nearestT);
    hit.Normal = length > 0.0f ? normal / length : Vec3d(0.0f, 1.0f, 0.0f);
    hit.Backface = nearestBackface;
    return true;
}

bool BakeBvh::FirstHitIsBackface(const Vec3d& origin, const Vec3d& direction,
                                 double maxT, double minT) const
{
    BakeBvhHit hit;
    return FirstHit(origin, direction, maxT, hit, minT) && hit.Backface;
}

bool BakeBvh::SegmentOccluded(const Vec3d& origin, const Vec3d& target) const
{
    if (Nodes.empty())
        return false;

    const Vec3d dir = target - origin;
    // Endpoints excluded: start just past the (already normal-offset) origin,
    // end just short of the light, so neither the emitting surface nor the
    // light counts as a blocker.
    constexpr double kEps = 1e-4;
    const double tMin = kEps;
    const double tMax = 1.0 - kEps;

    Vec3d invDir;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double d = dir[axis];
        invDir[axis] = static_cast<float>(
            (d > 0.0 || d < 0.0) ? 1.0 / d : 1e30);
    }

    std::array<std::uint32_t, 64> stack{};
    std::uint32_t depth = 0;
    stack[depth++] = 0;
    while (depth > 0)
    {
        const Node& node = Nodes[stack[--depth]];
        if (!SegmentIntersectsBox(node.Bounds, origin, invDir, tMin, tMax))
            continue;

        if (node.Count > 0)
        {
            for (std::uint32_t i = 0; i < node.Count; ++i)
            {
                const double t = RayTriangleT(
                    origin, dir, Triangles[Order[node.Start + i]]);
                if (t > tMin && t < tMax)
                    return true;
            }
            continue;
        }

        if (depth + 2 <= stack.size())
        {
            stack[depth++] = node.Left;
            stack[depth++] = node.Right;
        }
    }
    return false;
}
