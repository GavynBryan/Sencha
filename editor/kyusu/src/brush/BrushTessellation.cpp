#include "BrushTessellation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    // 2D cross product of (a - o) x (b - o): positive when o->a->b turns left.
    float Cross2(Vec2d o, Vec2d a, Vec2d b)
    {
        return (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);
    }

    // Loop positions projected into an in-plane basis. The basis handedness is
    // arbitrary; callers orient by the polygon's signed area instead.
    std::vector<Vec2d> ProjectLoop(const BrushMesh& mesh, const BrushFace& face)
    {
        Vec3d n = face.Normal;
        if (n.SqrMagnitude() <= 0.0f)
            n = { 0.0f, 0.0f, 1.0f };
        const Vec3d reference = std::abs(n.X) < 0.9f ? Vec3d{ 1.0f, 0.0f, 0.0f }
                                                     : Vec3d{ 0.0f, 1.0f, 0.0f };
        const Vec3d u = n.Cross(reference).Normalized();
        const Vec3d v = n.Cross(u);

        std::vector<Vec2d> points;
        points.reserve(face.Loop.size());
        for (const std::uint32_t index : face.Loop)
        {
            const Vec3d p = mesh.Vertices[index].Position;
            points.push_back(Vec2d{ p.Dot(u), p.Dot(v) });
        }
        return points;
    }

    float SignedArea2(const std::vector<Vec2d>& pts)
    {
        float area = 0.0f;
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const Vec2d& a = pts[i];
            const Vec2d& b = pts[(i + 1) % pts.size()];
            area += a.X * b.Y - b.X * a.Y;
        }
        return area;
    }

    // Convex including collinear runs: every turn agrees with `sign` or is flat.
    bool IsConvex(const std::vector<Vec2d>& pts, float sign, float eps)
    {
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const Vec2d& prev = pts[(i + pts.size() - 1) % pts.size()];
            const Vec2d& next = pts[(i + 1) % pts.size()];
            if (sign * Cross2(prev, pts[i], next) < -eps)
                return false;
        }
        return true;
    }

    // Ear-clips a simple polygon. Emits triangles as index triples into the
    // original loop order. Returns false when no ear exists (self-intersecting
    // or numerically degenerate input); the caller falls back to a fan.
    bool EarClip(const std::vector<Vec2d>& pts, float sign, float eps,
                 std::vector<std::array<std::uint32_t, 3>>& out)
    {
        std::vector<std::uint32_t> ring(pts.size());
        for (std::size_t i = 0; i < ring.size(); ++i)
            ring[i] = static_cast<std::uint32_t>(i);

        while (ring.size() > 3)
        {
            bool clipped = false;
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const std::uint32_t ia = ring[(i + ring.size() - 1) % ring.size()];
                const std::uint32_t ib = ring[i];
                const std::uint32_t ic = ring[(i + 1) % ring.size()];
                const Vec2d& a = pts[ia];
                const Vec2d& b = pts[ib];
                const Vec2d& c = pts[ic];

                const float turn = sign * Cross2(a, b, c);
                if (turn < -eps)
                    continue; // reflex vertex: not an ear

                // A flat turn is a collinear vertex: remove it without emitting
                // a degenerate triangle.
                if (turn <= eps)
                {
                    ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                    clipped = true;
                    break;
                }

                bool contains = false;
                for (const std::uint32_t other : ring)
                {
                    if (other == ia || other == ib || other == ic)
                        continue;
                    const Vec2d& q = pts[other];
                    if (sign * Cross2(a, b, q) >= -eps
                        && sign * Cross2(b, c, q) >= -eps
                        && sign * Cross2(c, a, q) >= -eps)
                    {
                        contains = true;
                        break;
                    }
                }
                if (contains)
                    continue;

                out.push_back({ ia, ib, ic });
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                clipped = true;
                break;
            }
            if (!clipped)
                return false;
        }

        // Final triangle, unless the remainder collapsed to collinear points.
        if (std::abs(Cross2(pts[ring[0]], pts[ring[1]], pts[ring[2]])) > eps)
            out.push_back({ ring[0], ring[1], ring[2] });
        return true;
    }

    void EmitFace(const BrushMesh& mesh, const Transform3f& transform, const BrushFace& face,
                  const BrushFaceEmit& emit, std::vector<BrushTriVertex>& triangles,
                  std::vector<std::array<std::uint32_t, 3>>& ears)
    {
        const std::size_t n = face.Loop.size();
        if (n < 3)
            return;

        const Vec3d worldNormal = transform.Rotation.RotateVector(face.Normal);

        const auto vertexAt = [&](std::uint32_t index) {
            const Vec3d local = mesh.Vertices[index].Position;
            return BrushTriVertex{
                .Position = transform.TransformPoint(local),
                .Normal = worldNormal,
                .Uv = ProjectUv(face.Material.Uv, local),
            };
        };

        triangles.clear();
        triangles.reserve((n - 2) * 3);

        const std::vector<Vec2d> pts = ProjectLoop(mesh, face);
        const float area = SignedArea2(pts);
        const float sign = area >= 0.0f ? 1.0f : -1.0f;
        // Relative tolerance: turns smaller than this fraction of the polygon's
        // area are treated as collinear.
        const float eps = std::abs(area) * 1e-6f;

        bool fan = true;
        if (n > 3 && !IsConvex(pts, sign, eps))
        {
            ears.clear();
            if (EarClip(pts, sign, eps, ears))
            {
                for (const std::array<std::uint32_t, 3>& tri : ears)
                {
                    triangles.push_back(vertexAt(face.Loop[tri[0]]));
                    triangles.push_back(vertexAt(face.Loop[tri[1]]));
                    triangles.push_back(vertexAt(face.Loop[tri[2]]));
                }
                fan = false;
            }
        }

        if (fan)
        {
            const BrushTriVertex base = vertexAt(face.Loop[0]);
            for (std::size_t i = 1; i + 1 < n; ++i)
            {
                triangles.push_back(base);
                triangles.push_back(vertexAt(face.Loop[i]));
                triangles.push_back(vertexAt(face.Loop[i + 1]));
            }
        }

        emit(face.Material, triangles);
    }
}

void BrushTessellate(const BrushMesh& mesh, const Transform3f& transform, const BrushFaceEmit& emit)
{
    std::vector<BrushTriVertex> triangles; // reused across faces
    std::vector<std::array<std::uint32_t, 3>> ears;

    for (const BrushFace& face : mesh.Faces)
    {
        EmitFace(mesh, transform, face, emit, triangles, ears);
    }
}

void BrushTessellateFace(const BrushMesh& mesh, const Transform3f& transform,
                         std::uint32_t faceIndex, const BrushFaceEmit& emit)
{
    if (faceIndex >= mesh.Faces.size())
        return;

    std::vector<BrushTriVertex> triangles;
    std::vector<std::array<std::uint32_t, 3>> ears;
    EmitFace(mesh, transform, mesh.Faces[faceIndex], emit, triangles, ears);
}
