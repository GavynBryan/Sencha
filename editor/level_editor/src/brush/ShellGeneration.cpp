#include "ShellGeneration.h"

#include "BrushHalfEdge.h"
#include "BrushValidation.h"
#include "CarveSurround.h"

#include <math/geometry/3d/Plane.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    constexpr float kPlanarTolerance = 1e-3f;
    constexpr float kParallel = 1e-4f;
    constexpr float kShareTolerance = 1e-3f;
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

    // A face's plane as 2D, counter-clockwise seen from outside.
    struct PlaneBasis
    {
        Vec3d Origin;
        Vec3d U;
        Vec3d V;

        PlaneBasis(const Plane& plane, const Vec3d& origin)
            : Origin(origin)
        {
            const Vec3d n = plane.Normal;
            const Vec3d axis = std::abs(n.X) < 0.9f ? Vec3d{ 1, 0, 0 } : Vec3d{ 0, 1, 0 };
            U = n.Cross(axis).Normalized();
            V = n.Cross(U);
        }

        [[nodiscard]] Vec2d To(const Vec3d& point) const
        {
            const Vec3d d = point - Origin;
            return { d.Dot(U), d.Dot(V) };
        }
        [[nodiscard]] Vec3d From(const Vec2d& point) const { return Origin + U * point.X + V * point.Y; }
    };

    // The parts of `polygon` outside the convex `cut`, as convex pieces: the
    // part beyond each edge of the cut that is inside the edges before it.
    std::vector<std::vector<Vec2d>> Outside(const std::vector<Vec2d>& polygon, const std::vector<Vec2d>& cut)
    {
        constexpr float kFar = 1e5f;
        const auto halfPlane = [&](Vec2d a, Vec2d b, bool beyond) {
            const Vec2d along = (b - a).Normalized();
            const Vec2d left{ -along.Y, along.X };
            const Vec2d side = beyond ? left * -kFar : left * kFar;
            return std::vector<Vec2d>{ a - along * kFar, b + along * kFar, b + along * kFar + side,
                                       a - along * kFar + side };
        };
        std::vector<std::vector<Vec2d>> pieces;
        for (std::size_t j = 0; j < cut.size(); ++j)
        {
            std::vector<Vec2d> piece = polygon;
            for (std::size_t k = 0; k <= j && piece.size() >= 3; ++k)
            {
                std::vector<Vec2d> region = halfPlane(cut[k], cut[(k + 1) % cut.size()], k == j);
                if (k == j)
                    std::reverse(region.begin(), region.end());
                piece = ClipPolygonToConvex2D(piece, region, kShareTolerance);
            }
            if (piece.size() >= 3 && PolygonSignedArea(piece) > kShareTolerance)
                pieces.push_back(std::move(piece));
        }
        return pieces;
    }

    struct Region
    {
        std::vector<Vec2d> Outline;
        // The neighbour face this region is shared with, when it is.
        const BrushFace*   Across = nullptr;
        bool               Keep = true;
    };

    // The slab over `outline` (on the face plane) between the inner and outer
    // planes. An edge along one of the face's own edges takes that edge's join
    // plane; any other edge is a seam with a neighbouring piece, capped square.
    std::optional<BrushMesh> BuildSlab(const PlaneBasis& basis, const std::vector<Vec2d>& outline,
                                       const std::vector<Vec2d>& face, const std::vector<Plane>& joins,
                                       const Plane& inner, const Plane& outer, const FaceMaterial& material,
                                       const FaceMaterial& outsideMaterial)
    {
        const std::size_t count = outline.size();
        std::vector<Plane> edges(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vec2d a = outline[i];
            const Vec2d b = outline[(i + 1) % count];
            std::optional<std::size_t> along;
            for (std::size_t k = 0; k < face.size() && !along; ++k)
            {
                const Vec2d p = face[k];
                const Vec2d direction = (face[(k + 1) % face.size()] - p).Normalized();
                const auto off = [&](Vec2d q) { return std::abs((q - p).X * direction.Y - (q - p).Y * direction.X); };
                if (off(a) <= kShareTolerance && off(b) <= kShareTolerance && (b - a).Dot(direction) > 0.0f)
                    along = k;
            }
            const Vec3d from = basis.From(a);
            edges[i] = along ? joins[*along]
                             : Plane::FromNormalAndPoint((basis.From(b) - from).Cross(inner.Normal).Normalized(), from);
        }

        BrushMesh mesh;
        mesh.Vertices.resize(count * 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            const Plane& before = edges[(i + count - 1) % count];
            const Vec3d vertex = basis.From(outline[i]);
            const std::optional<Vec3d> insideCorner = Corner(before, edges[i], inner, vertex);
            const std::optional<Vec3d> outsideCorner = Corner(before, edges[i], outer, vertex);
            if (!insideCorner || !outsideCorner)
                return std::nullopt;
            mesh.Vertices[i].Position = *insideCorner;
            mesh.Vertices[count + i].Position = *outsideCorner;
        }
        // A slab inverts when its outer rim runs against its inner one: the
        // thickness exceeds a feature of the face.
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t next = (i + 1) % count;
            const Vec3d innerEdge = mesh.Vertices[next].Position - mesh.Vertices[i].Position;
            const Vec3d outerEdge = mesh.Vertices[count + next].Position - mesh.Vertices[count + i].Position;
            if (innerEdge.Dot(outerEdge) < 0.0f)
                return std::nullopt;
        }

        BrushFace inside{ {}, {}, material };
        BrushFace outside{ {}, {}, outsideMaterial };
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
                                            {}, material });
        }
        BrushOrientFacesOutward(mesh);
        (void)BrushValidateAndRepair(mesh);
        return mesh;
    }

    // Splits the face's outline wherever a neighbour face lies across the gap,
    // facing back: the shared part becomes its own region, kept by the owner.
    std::vector<Region> Regions(const PlaneBasis& basis, const std::vector<Vec2d>& face, const Plane& inner,
                                float thickness, std::span<const ShellNeighbour> neighbours, std::uint32_t f,
                                ShellResult& result)
    {
        std::vector<Region> regions{ Region{ face } };
        for (const ShellNeighbour& neighbour : neighbours)
        {
            if (neighbour.Volume == nullptr || std::abs(neighbour.Thickness - thickness) > kShareTolerance)
                continue;
            for (const BrushFace& across : neighbour.Volume->Faces)
            {
                if (BrushComputeFaceNormal(*neighbour.Volume, across).Dot(inner.Normal) > -1.0f + kParallel)
                    continue;
                const bool onOuterPlane = std::all_of(across.Loop.begin(), across.Loop.end(), [&](std::uint32_t v) {
                    return std::abs(inner.SignedDistanceTo(neighbour.Volume->Vertices[v].Position) - thickness)
                        <= kShareTolerance;
                });
                if (!onOuterPlane)
                    continue;
                std::vector<Vec2d> theirs;
                for (auto v = across.Loop.rbegin(); v != across.Loop.rend(); ++v)
                    theirs.push_back(basis.To(neighbour.Volume->Vertices[*v].Position));
                const std::vector<Vec2d> shared = ClipPolygonToConvex2D(face, theirs, kShareTolerance);
                if (shared.size() < 3 || PolygonSignedArea(shared) <= kShareTolerance)
                    continue;
                if (!IsConvexPolygon2D(theirs, kShareTolerance) || !IsConvexPolygon2D(face, kShareTolerance))
                {
                    result.Problems.push_back({ ShellProblemKind::SharedWallNotConvex, f,
                                                "the wall shared across face " + std::to_string(f)
                                                    + " is not convex, so it stands twice" });
                    continue;
                }

                std::vector<Region> split;
                for (Region& region : regions)
                {
                    if (region.Across != nullptr)
                    {
                        split.push_back(std::move(region));
                        continue;
                    }
                    std::vector<Vec2d> inside = ClipPolygonToConvex2D(region.Outline, shared, kShareTolerance);
                    if (inside.size() >= 3 && PolygonSignedArea(inside) > kShareTolerance)
                        split.push_back(Region{ std::move(inside), &across, neighbour.OwnsShared });
                    for (std::vector<Vec2d>& piece : Outside(region.Outline, shared))
                        split.push_back(Region{ std::move(piece) });
                }
                regions = std::move(split);
            }
        }
        return regions;
    }

    void Refuse(ShellResult& result, ShellProblemKind kind, std::uint32_t face, std::string message)
    {
        result.Problems.push_back({ kind, face, std::move(message) });
        result.Slabs.clear();
        result.Refused = true;
    }
}

ShellResult GenerateShell(const BrushMesh& volume, const ShellRules& rules,
                          std::span<const ShellNeighbour> neighbours)
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

        const PlaneBasis basis(faces[f].Inner, volume.Vertices[face.Loop.front()].Position);
        std::vector<Vec2d> outline;
        for (const std::uint32_t vertex : face.Loop)
            outline.push_back(basis.To(volume.Vertices[vertex].Position));

        for (const Region& region : Regions(basis, outline, faces[f].Inner, rules.Thickness, neighbours, f, result))
        {
            if (!region.Keep)
                continue;
            std::optional<BrushMesh> mesh =
                BuildSlab(basis, region.Outline, outline, joins, faces[f].Inner, outer, face.Material,
                          region.Across != nullptr ? region.Across->Material : face.Material);
            if (!mesh)
            {
                Refuse(result, ShellProblemKind::InvertedSlab, f,
                       "face " + std::to_string(f) + " is narrower than twice the wall thickness");
                return result;
            }
            ShellSlab slab{ std::move(*mesh), f, {} };
            for (std::uint32_t g = 0; g < slab.Mesh.Faces.size(); ++g)
                if (slab.Mesh.Faces[g].Normal.Dot(faces[f].Inner.Normal) < -1.0f + kParallel)
                    slab.InnerFaces.push_back(g);
            result.Slabs.push_back(std::move(slab));
        }
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
