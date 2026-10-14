#include "ShellGeneration.h"

#include "BrushHalfEdge.h"
#include "BrushValidation.h"

#include <math/geometry/3d/Plane.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace
{
    constexpr float kPlanarTolerance = 1e-3f;
    constexpr float kParallel = 1e-4f;
    const float kHorizontalCosine = std::sqrt(0.5f);

    enum class FaceClass : std::uint8_t
    {
        Wall,
        Horizontal,
    };

    struct FaceInfo
    {
        Plane     Inner;
        FaceClass Class = FaceClass::Wall;
        bool      Open = false;
    };

    Plane Offset(const Plane& plane, float distance)
    {
        return Plane(plane.Normal, plane.D - distance);
    }

    // The plane a slab on `face` is bounded by along its edge `from -> to`,
    // which it shares with `neighbour`.
    Plane JoinPlane(const FaceInfo& face, const FaceInfo& neighbour, const Vec3d& from, const Vec3d& to,
                    float thickness)
    {
        if (neighbour.Open)
            return neighbour.Inner;
        if (face.Class == FaceClass::Horizontal && neighbour.Class == FaceClass::Wall)
            return Offset(neighbour.Inner, thickness);
        if (face.Class == FaceClass::Wall && neighbour.Class == FaceClass::Horizontal)
            return neighbour.Inner;
        const Vec3d along = (to - from).Normalized();
        const Vec3d outward = face.Inner.Normal + neighbour.Inner.Normal;
        Vec3d normal = along.Cross(outward);
        if (normal.Magnitude() <= kParallel)
            normal = along.Cross(face.Inner.Normal);
        return Plane::FromNormalAndPoint(normal.Normalized(), from);
    }

    std::optional<Vec3d> Corner(const Plane& before, const Plane& after, const Plane& level, const Vec3d& vertex)
    {
        if (std::optional<Vec3d> point = IntersectPlanes(before, after, level, kParallel))
            return point;
        // Two edges along one line: their join planes coincide, so the corner
        // is where the vertex meets `level` within that plane.
        const Vec3d start = after.ClosestPoint(vertex);
        const Vec3d inPlane = level.Normal - after.Normal * level.Normal.Dot(after.Normal);
        const float rate = level.Normal.Dot(inPlane);
        if (std::abs(rate) <= kParallel)
            return std::nullopt;
        return start - inPlane * (level.SignedDistanceTo(start) / rate);
    }

    void Refuse(ShellResult& result, ShellProblemKind kind, std::uint32_t face, std::string message)
    {
        result.Problems.push_back({ kind, face, std::move(message) });
        result.Slabs.clear();
        result.Refused = true;
    }
}

ShellResult GenerateShell(const BrushMesh& volume, const ShellRules& rules)
{
    ShellResult result;
    const BrushHalfEdgeMesh halfEdges = BrushBuildHalfEdge(volume);
    for (const BrushHalfEdge& halfEdge : halfEdges.HalfEdges)
        if (halfEdge.Twin == BrushInvalidIndex)
        {
            Refuse(result, ShellProblemKind::OpenVolume, halfEdge.Face, "the volume is not closed");
            return result;
        }

    std::vector<FaceInfo> faces(volume.Faces.size());
    for (std::uint32_t f = 0; f < volume.Faces.size(); ++f)
    {
        const BrushFace& face = volume.Faces[f];
        const Vec3d normal = BrushComputeFaceNormal(volume, face);
        FaceInfo& info = faces[f];
        info.Inner = Plane::FromNormalAndPoint(normal, volume.Vertices[face.Loop.front()].Position);
        for (const std::uint32_t vertex : face.Loop)
            if (std::abs(info.Inner.SignedDistanceTo(volume.Vertices[vertex].Position)) > kPlanarTolerance)
            {
                Refuse(result, ShellProblemKind::NonPlanarFace, f, "face " + std::to_string(f) + " is not flat");
                return result;
            }
        info.Class = std::abs(normal.Dot(rules.Up)) > kHorizontalCosine ? FaceClass::Horizontal : FaceClass::Wall;
        info.Open = SameAsset(face.Material.Material, rules.OpenFaceMaterial);
    }

    for (std::uint32_t f = 0; f < volume.Faces.size(); ++f)
    {
        if (faces[f].Open)
            continue;
        const BrushFace& face = volume.Faces[f];
        const std::size_t count = face.Loop.size();
        const Plane outer = Offset(faces[f].Inner, rules.Thickness);

        // The join plane along each edge i, from Loop[i] to Loop[i + 1].
        std::vector<Plane> joins(count);
        std::uint32_t halfEdge = halfEdges.FaceToHalfEdge[f];
        for (std::size_t i = 0; i < count; ++i)
        {
            const BrushHalfEdge& edge = halfEdges.HalfEdges[halfEdge];
            const std::uint32_t neighbour = halfEdges.HalfEdges[edge.Twin].Face;
            const Vec3d& from = volume.Vertices[edge.Origin].Position;
            const Vec3d& to = volume.Vertices[halfEdges.HalfEdges[edge.Next].Origin].Position;
            const std::size_t at = static_cast<std::size_t>(
                std::find(face.Loop.begin(), face.Loop.end(), edge.Origin) - face.Loop.begin());
            joins[at] = JoinPlane(faces[f], faces[neighbour], from, to, rules.Thickness);
            halfEdge = edge.Next;
        }

        ShellSlab slab{ {}, f };
        BrushMesh& mesh = slab.Mesh;
        mesh.Vertices.resize(count * 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            const Plane& before = joins[(i + count - 1) % count];
            const Vec3d& vertex = volume.Vertices[face.Loop[i]].Position;
            const std::optional<Vec3d> inner = Corner(before, joins[i], faces[f].Inner, vertex);
            const std::optional<Vec3d> outside = Corner(before, joins[i], outer, vertex);
            if (!inner || !outside)
            {
                Refuse(result, ShellProblemKind::InvertedSlab, f,
                       "face " + std::to_string(f) + " has a corner the walls cannot meet at");
                return result;
            }
            mesh.Vertices[i].Position = *inner;
            mesh.Vertices[count + i].Position = *outside;
        }

        // A slab inverts when its outer rim runs against its inner one: the
        // thickness exceeds a feature of the face.
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t next = (i + 1) % count;
            const Vec3d innerEdge = mesh.Vertices[next].Position - mesh.Vertices[i].Position;
            const Vec3d outerEdge = mesh.Vertices[count + next].Position - mesh.Vertices[count + i].Position;
            if (innerEdge.Dot(outerEdge) < 0.0f)
            {
                Refuse(result, ShellProblemKind::InvertedSlab, f,
                       "face " + std::to_string(f) + " is narrower than twice the wall thickness");
                return result;
            }
        }

        BrushFace inside{ {}, {}, face.Material };
        BrushFace outside{ {}, {}, face.Material };
        for (std::size_t i = 0; i < count; ++i)
        {
            outside.Loop.push_back(static_cast<std::uint32_t>(count + i));
            inside.Loop.push_back(static_cast<std::uint32_t>(count - 1 - i));
        }
        mesh.Faces.push_back(std::move(inside));
        mesh.Faces.push_back(std::move(outside));
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(i);
            const std::uint32_t b = static_cast<std::uint32_t>((i + 1) % count);
            mesh.Faces.push_back(BrushFace{ { a, b, static_cast<std::uint32_t>(count) + b,
                                              static_cast<std::uint32_t>(count) + a },
                                            {}, face.Material });
        }
        BrushOrientFacesOutward(mesh);
        (void)BrushValidateAndRepair(mesh);
        result.Slabs.push_back(std::move(slab));
    }

    // Count the distinct planes at each corner; past three, bisectors are an
    // approximation there.
    for (std::uint32_t vertex = 0; vertex < volume.Vertices.size(); ++vertex)
    {
        std::vector<Vec3d> normals;
        for (std::uint32_t f = 0; f < volume.Faces.size(); ++f)
        {
            const std::vector<std::uint32_t>& loop = volume.Faces[f].Loop;
            if (std::find(loop.begin(), loop.end(), vertex) == loop.end())
                continue;
            const Vec3d& normal = faces[f].Inner.Normal;
            if (std::none_of(normals.begin(), normals.end(),
                             [&](const Vec3d& seen) { return seen.Dot(normal) > 1.0f - kParallel; }))
                normals.push_back(normal);
        }
        if (normals.size() > 3)
            result.Problems.push_back({ ShellProblemKind::VertexJoin, vertex,
                                        std::to_string(normals.size()) + " planes meet at a corner" });
    }
    return result;
}
