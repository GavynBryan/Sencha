#include "ShellGeneration.h"

#include "BrushFaceFrame.h"
#include "BrushHalfEdge.h"
#include "BrushValidation.h"
#include "CarvePolygon.h"
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
        // The neighbour face this region is shared with, when it is, and that
        // neighbour's index.
        const BrushFace*   Across = nullptr;
        bool               Keep = true;
        std::optional<std::uint32_t> Neighbour;
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
        std::vector<Region> regions{ Region{ face, nullptr, true, std::nullopt } };
        for (std::uint32_t index = 0; index < neighbours.size(); ++index)
        {
            const ShellNeighbour& neighbour = neighbours[index];
            if (neighbour.Volume == nullptr || std::abs(neighbour.Thickness - thickness) > kShareTolerance)
                continue;
            for (const BrushFace& across : neighbour.Volume->Faces)
            {
                if (BrushComputeFaceNormal(*neighbour.Volume, across).Dot(inner.Normal) > -1.0f + kParallel)
                    continue;
                const float gap = inner.SignedDistanceTo(neighbour.Volume->Vertices[across.Loop.front()].Position);
                const bool parallel = std::all_of(across.Loop.begin(), across.Loop.end(), [&](std::uint32_t v) {
                    return std::abs(inner.SignedDistanceTo(neighbour.Volume->Vertices[v].Position) - gap)
                        <= kShareTolerance;
                });
                if (!parallel || gap <= kShareTolerance || gap > thickness + kShareTolerance)
                    continue;
                std::vector<Vec2d> theirs;
                for (auto v = across.Loop.rbegin(); v != across.Loop.rend(); ++v)
                    theirs.push_back(basis.To(neighbour.Volume->Vertices[*v].Position));
                const std::vector<Vec2d> shared = ClipPolygonToConvex2D(face, theirs, kShareTolerance);
                if (shared.size() < 3 || PolygonSignedArea(shared) <= kShareTolerance)
                    continue;
                if (gap < thickness - kShareTolerance)
                {
                    result.Problems.push_back({ ShellProblemKind::WallsOverlap, f,
                                                "a neighbour is " + std::to_string(gap) + " m from face "
                                                    + std::to_string(f) + ", nearer than a wall is thick" });
                    continue;
                }
                if (!IsConvexPolygon2D(theirs, kShareTolerance) || !IsConvexPolygon2D(face, kShareTolerance))
                {
                    result.Problems.push_back({ ShellProblemKind::SharedWallNotConvex, f,
                                                "the wall shared across face " + std::to_string(f)
                                                    + " is not convex, so it stands twice" });
                    continue;
                }

                if (std::find(result.SharedWith.begin(), result.SharedWith.end(), index) == result.SharedWith.end())
                    result.SharedWith.push_back(index);
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
                        split.push_back(Region{ std::move(inside), &across, neighbour.OwnsShared, index });
                    for (std::vector<Vec2d>& piece : Outside(region.Outline, shared))
                        split.push_back(Region{ std::move(piece), nullptr, true, std::nullopt });
                }
                regions = std::move(split);
            }
        }
        return regions;
    }

    // Whether every vertex lies on or behind every face's plane.
    bool IsConvexVolume(const BrushMesh& mesh)
    {
        for (const BrushFace& face : mesh.Faces)
        {
            const Plane plane = Plane::FromNormalAndPoint(BrushComputeFaceNormal(mesh, face),
                                                          mesh.Vertices[face.Loop.front()].Position);
            for (const BrushVertex& vertex : mesh.Vertices)
                if (plane.SignedDistanceTo(vertex.Position) > kShareTolerance)
                    return false;
        }
        return true;
    }

    // Whether `b` lies wholly outside one of `a`'s face planes.
    bool SeparatedByAFaceOf(const BrushMesh& a, const BrushMesh& b)
    {
        for (const BrushFace& face : a.Faces)
        {
            const Plane plane = Plane::FromNormalAndPoint(BrushComputeFaceNormal(a, face),
                                                          a.Vertices[face.Loop.front()].Position);
            if (std::all_of(b.Vertices.begin(), b.Vertices.end(), [&](const BrushVertex& vertex) {
                    return plane.SignedDistanceTo(vertex.Position) >= -kShareTolerance;
                }))
                return true;
        }
        return false;
    }

    // Two convex volumes share interior unless a face plane of either
    // separates them. Faces alone settle boxes and prisms standing upright;
    // a non-convex pair is not judged.
    bool ConvexVolumesIntersect(const BrushMesh& a, const BrushMesh& b)
    {
        return IsConvexVolume(a) && IsConvexVolume(b) && !SeparatedByAFaceOf(a, b) && !SeparatedByAFaceOf(b, a);
    }

    void Refuse(ShellResult& result, ShellProblemKind kind, std::uint32_t face, std::string message)
    {
        result.Problems.push_back({ kind, face, std::move(message) });
        result.Slabs.clear();
        result.SharedWith.clear();
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

    for (const ShellNeighbour& neighbour : neighbours)
    {
        if (neighbour.Volume != nullptr && ConvexVolumesIntersect(volume, *neighbour.Volume))
        {
            Refuse(result, ShellProblemKind::SpaceOverlap, 0, "it overlaps another shelled space");
            return result;
        }
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
            ShellSlab slab{ std::move(*mesh), f, faces[f].Inner.Normal, {}, region.Neighbour, {} };
            for (std::uint32_t g = 0; g < slab.Mesh.Faces.size(); ++g)
            {
                const BrushFace& built = slab.Mesh.Faces[g];
                const float facing = built.Normal.Dot(faces[f].Inner.Normal);
                if (facing < -1.0f + kParallel)
                    slab.InnerFaces.push_back(g);
                else if (facing < 1.0f - kParallel)
                    slab.Ends.push_back(Plane::FromNormalAndPoint(built.Normal, slab.Mesh.Vertices[built.Loop.front()].Position));
            }
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

namespace
{
    // How square to a wall an opening must run to cut it.
    constexpr float kFacingCosine = 0.9f;

    std::vector<std::uint32_t> FacesFacing(const BrushMesh& mesh, const Vec3d& normal)
    {
        std::vector<std::uint32_t> faces;
        for (std::uint32_t g = 0; g < mesh.Faces.size(); ++g)
            if (mesh.Faces[g].Normal.Dot(normal) > 1.0f - kParallel)
                faces.push_back(g);
        return faces;
    }

    // The opening's outline on the face `frame` stands for, carried there along
    // its through axis and wound counter-clockwise.
    std::vector<Vec2d> OutlineOn(const BrushFaceFrame& frame, const ShellOpening& opening)
    {
        const Vec2d min{ -0.5f * opening.Width, 0.0f };
        const Vec2d max{ 0.5f * opening.Width, opening.Height };
        const Plane plane = Plane::FromNormalAndPoint(frame.Normal, frame.Origin);
        const float rate = plane.Normal.Dot(opening.Through);
        std::vector<Vec2d> outline;
        for (const Vec2d& point : CarveShapeOutline(opening.Shape, min, max, opening.Params))
        {
            const Vec3d at = opening.Origin + opening.Across * point.X + opening.Up * point.Y;
            outline.push_back(frame.ToFrame(at - opening.Through * (plane.SignedDistanceTo(at) / rate)));
        }
        if (PolygonSignedArea(outline) < 0.0f)
            std::reverse(outline.begin(), outline.end());
        return outline;
    }
}

void CutOpenings(ShellResult& shell, float thickness, std::span<const ShellOpening> openings)
{
    for (ShellSlab& slab : shell.Slabs)
    {
        const std::vector<std::uint32_t> outer = FacesFacing(slab.Mesh, slab.Outward);
        if (outer.size() != 1)
            continue;
        const BrushFaceFrameResult framed = FaceFrame(slab.Mesh, outer.front(), kPlanarTolerance);
        if (!framed.Frame)
            continue;
        const BrushFaceFrame& frame = *framed.Frame;
        // Every opening is clipped to the slab as it stood before any was cut,
        // so the pieces an earlier cut left cover a later one exactly.
        const std::vector<Vec2d> rim = frame.Outline;
        const Plane outerPlane = Plane::FromNormalAndPoint(frame.Normal, frame.Origin);

        bool cut = false;
        for (std::uint32_t index = 0; index < openings.size(); ++index)
        {
            const ShellOpening& opening = openings[index];
            if (std::abs(opening.Through.Dot(slab.Outward)) < kFacingCosine)
                continue;
            const float depth = -outerPlane.SignedDistanceTo(opening.Origin);
            if (depth < -kOpeningReach || depth > thickness + kOpeningReach)
                continue;
            const std::vector<Vec2d> outline = ClipPolygonToConvex2D(OutlineOn(frame, opening), rim, kShareTolerance);
            if (outline.size() < 3 || PolygonSignedArea(outline) <= kShareTolerance)
                continue;

            std::vector<FaceCorners> surface;
            for (const std::uint32_t face : CoplanarSurface(slab.Mesh, FacesFacing(slab.Mesh, slab.Outward).front(),
                                                            kPlanarTolerance))
                surface.push_back(CornersOf(slab.Mesh, face));
            CarveOutcome carved = CarveAcrossSurface(slab.Mesh, surface, frame, outline, true, kPlanarTolerance);
            if (!carved.Ok())
            {
                shell.Problems.push_back({ ShellProblemKind::OpeningRefused, index,
                                           std::string("opening ") + std::to_string(index)
                                               + " could not be cut: " + CarveStatusText(carved.Status()) });
                continue;
            }
            slab.Mesh = std::move(carved.Take().Mesh);
            cut = true;
        }
        if (cut)
            slab.InnerFaces = FacesFacing(slab.Mesh, slab.Outward * -1.0f);
    }
}

namespace
{
    bool OnAnyPlane(const BrushMesh& mesh, const BrushFace& face, std::span<const Plane> planes)
    {
        const Vec3d normal = BrushComputeFaceNormal(mesh, face);
        return std::any_of(planes.begin(), planes.end(), [&](const Plane& plane) {
            return normal.Dot(plane.Normal) > 1.0f - kParallel
                && std::all_of(face.Loop.begin(), face.Loop.end(), [&](std::uint32_t v) {
                       return std::abs(plane.SignedDistanceTo(mesh.Vertices[v].Position)) <= kPlanarTolerance;
                   });
        });
    }

    // The faces of `mesh` listed, with only the vertices they use.
    BrushMesh FacesOf(const BrushMesh& mesh, std::span<const std::uint32_t> faces)
    {
        BrushMesh part;
        std::vector<std::uint32_t> remap(mesh.Vertices.size(), BrushInvalidIndex);
        for (const std::uint32_t f : faces)
        {
            BrushFace face = mesh.Faces[f];
            for (std::uint32_t& v : face.Loop)
            {
                if (remap[v] == BrushInvalidIndex)
                {
                    remap[v] = static_cast<std::uint32_t>(part.Vertices.size());
                    part.Vertices.push_back(mesh.Vertices[v]);
                }
                v = remap[v];
            }
            part.Faces.push_back(std::move(face));
        }
        return part;
    }
}

std::vector<ShellSurface> ShellSurfaces(const ShellResult& shell)
{
    std::vector<ShellSurface> surfaces;
    for (std::uint32_t index = 0; index < shell.Slabs.size(); ++index)
    {
        const ShellSlab& slab = shell.Slabs[index];
        std::vector<std::uint32_t> sides[3];
        for (std::uint32_t f = 0; f < slab.Mesh.Faces.size(); ++f)
        {
            const BrushFace& face = slab.Mesh.Faces[f];
            const float facing = BrushComputeFaceNormal(slab.Mesh, face).Dot(slab.Outward);
            ShellSide side = ShellSide::Outside;
            if (facing < -1.0f + kParallel)
                side = ShellSide::Room;
            else if (facing > 1.0f - kParallel)
                side = slab.SharedWith ? ShellSide::NeighbourRoom : ShellSide::Outside;
            else if (!OnAnyPlane(slab.Mesh, face, slab.Ends))
                side = ShellSide::Room; // a reveal: the side of an opening cut through it
            sides[static_cast<std::size_t>(side)].push_back(f);
        }
        for (std::size_t side = 0; side < 3; ++side)
            if (!sides[side].empty())
                surfaces.push_back({ index, static_cast<ShellSide>(side), FacesOf(slab.Mesh, sides[side]) });
    }
    return surfaces;
}
