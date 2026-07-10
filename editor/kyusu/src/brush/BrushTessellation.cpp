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

    // Per-loop-vertex LOCAL normals for one face, or empty to shade the face
    // hard (the cached face normal everywhere).
    using LoopNormals = std::vector<Vec3d>;

    // Smooth-shading normals across soft edges: for every vertex of every
    // face, the faces around that vertex connected through soft edges form a
    // smoothing group whose averaged normal replaces the flat face normal.
    // Only computed when the mesh has soft edges; entries stay empty (hard)
    // for faces no soft edge touches.
    std::vector<LoopNormals> ComputeSoftLoopNormals(const BrushMesh& mesh)
    {
        std::vector<LoopNormals> perFace(mesh.Faces.size());
        if (mesh.SoftEdges.empty())
            return perFace;

        // Vertex -> (face, loop position) incidences.
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> facesAt(mesh.Vertices.size());
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            for (std::uint32_t i = 0; i < loop.size(); ++i)
                if (loop[i] < facesAt.size())
                    facesAt[loop[i]].push_back({ f, i });
        }

        // Vertices touched by any soft edge.
        std::vector<bool> softVertex(mesh.Vertices.size(), false);
        for (const std::array<std::uint32_t, 2>& edge : mesh.SoftEdges)
        {
            if (edge[0] < softVertex.size())
                softVertex[edge[0]] = true;
            if (edge[1] < softVertex.size())
                softVertex[edge[1]] = true;
        }

        for (std::uint32_t v = 0; v < mesh.Vertices.size(); ++v)
        {
            if (!softVertex[v] || facesAt[v].empty())
                continue;

            // Group the faces around v: two faces link when they share a soft
            // edge incident to v. Small counts; a flat union-find suffices.
            const auto& incident = facesAt[v];
            std::vector<std::uint32_t> group(incident.size());
            for (std::uint32_t i = 0; i < group.size(); ++i)
                group[i] = i;
            const auto rootOf = [&](std::uint32_t i)
            {
                while (group[i] != i)
                    i = group[i] = group[group[i]];
                return i;
            };

            for (std::size_t i = 0; i < incident.size(); ++i)
                for (std::size_t j = i + 1; j < incident.size(); ++j)
                {
                    const auto& fi = mesh.Faces[incident[i].first].Loop;
                    const auto& fj = mesh.Faces[incident[j].first].Loop;
                    // Shared soft edge at v: a neighbor vertex of v present in
                    // both loops adjacent to v, whose (v, x) pair is soft.
                    bool linked = false;
                    const auto neighborsAt = [v](const std::vector<std::uint32_t>& loop,
                                                 std::uint32_t x)
                    {
                        for (std::size_t k = 0; k < loop.size(); ++k)
                            if (loop[k] == v)
                            {
                                const std::uint32_t prev = loop[(k + loop.size() - 1) % loop.size()];
                                const std::uint32_t next = loop[(k + 1) % loop.size()];
                                if (prev == x || next == x)
                                    return true;
                            }
                        return false;
                    };
                    for (const std::array<std::uint32_t, 2>& soft : mesh.SoftEdges)
                    {
                        if (soft[0] != v && soft[1] != v)
                            continue;
                        const std::uint32_t other = soft[0] == v ? soft[1] : soft[0];
                        if (neighborsAt(fi, other) && neighborsAt(fj, other))
                        {
                            linked = true;
                            break;
                        }
                    }
                    if (linked)
                        group[rootOf(static_cast<std::uint32_t>(i))] = rootOf(static_cast<std::uint32_t>(j));
                }

            // Averaged normal per group, written back to every member face.
            std::vector<Vec3d> groupNormal(incident.size(), Vec3d{ 0.0f, 0.0f, 0.0f });
            std::vector<int> groupCount(incident.size(), 0);
            for (std::size_t i = 0; i < incident.size(); ++i)
            {
                const std::uint32_t root = rootOf(static_cast<std::uint32_t>(i));
                groupNormal[root] += mesh.Faces[incident[i].first].Normal;
                ++groupCount[root];
            }
            for (std::size_t i = 0; i < incident.size(); ++i)
            {
                const std::uint32_t root = rootOf(static_cast<std::uint32_t>(i));
                if (groupCount[root] < 2)
                    continue; // alone in its group: stays hard
                const Vec3d sum = groupNormal[root];
                if (sum.SqrMagnitude() <= 0.0f)
                    continue;
                LoopNormals& normals = perFace[incident[i].first];
                if (normals.empty())
                    normals.assign(mesh.Faces[incident[i].first].Loop.size(),
                                   mesh.Faces[incident[i].first].Normal);
                normals[incident[i].second] = sum.Normalized();
            }
        }
        return perFace;
    }

    void EmitFace(const BrushMesh& mesh, const Transform3f& transform, const BrushFace& face,
                  const BrushFaceEmit& emit, std::vector<BrushTriVertex>& triangles,
                  std::vector<std::array<std::uint32_t, 3>>& ears,
                  const LoopNormals* loopNormals = nullptr)
    {
        const std::size_t n = face.Loop.size();
        if (n < 3)
            return;

        const Vec3d worldNormal = transform.Rotation.RotateVector(face.Normal);

        // Loop-position addressing so smoothed normals can differ per corner.
        const auto vertexAtLoop = [&](std::uint32_t loopIdx) {
            const std::uint32_t index = face.Loop[loopIdx];
            const Vec3d local = mesh.Vertices[index].Position;
            const Vec3d normal = loopNormals != nullptr && loopIdx < loopNormals->size()
                ? transform.Rotation.RotateVector((*loopNormals)[loopIdx])
                : worldNormal;
            return BrushTriVertex{
                .Position = transform.TransformPoint(local),
                .Normal = normal,
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
                    triangles.push_back(vertexAtLoop(tri[0]));
                    triangles.push_back(vertexAtLoop(tri[1]));
                    triangles.push_back(vertexAtLoop(tri[2]));
                }
                fan = false;
            }
        }

        if (fan)
        {
            const BrushTriVertex base = vertexAtLoop(0);
            for (std::size_t i = 1; i + 1 < n; ++i)
            {
                triangles.push_back(base);
                triangles.push_back(vertexAtLoop(static_cast<std::uint32_t>(i)));
                triangles.push_back(vertexAtLoop(static_cast<std::uint32_t>(i + 1)));
            }
        }

        emit(face.Material, triangles);
    }
}

void BrushTessellate(const BrushMesh& mesh, const Transform3f& transform, const BrushFaceEmit& emit)
{
    std::vector<BrushTriVertex> triangles; // reused across faces
    std::vector<std::array<std::uint32_t, 3>> ears;
    const std::vector<LoopNormals> soft = ComputeSoftLoopNormals(mesh);

    for (std::size_t f = 0; f < mesh.Faces.size(); ++f)
    {
        const LoopNormals* normals = f < soft.size() && !soft[f].empty() ? &soft[f] : nullptr;
        EmitFace(mesh, transform, mesh.Faces[f], emit, triangles, ears, normals);
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
