#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
constexpr float kPlanarTol = 1e-3f;

std::uint32_t FaceFacing(const BrushMesh& mesh, Vec3d direction)
{
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(mesh, mesh.Faces[i]).Dot(direction) > 0.99f)
            return i;
    return 0xFFFFFFFFu;
}

struct Bounds
{
    Vec2d Min, Max;
};

Bounds OutlineBounds(const BrushFaceFrame& frame)
{
    Bounds bounds{ frame.Outline.front(), frame.Outline.front() };
    for (const Vec2d& p : frame.Outline)
    {
        bounds.Min.X = std::min(bounds.Min.X, p.X);
        bounds.Min.Y = std::min(bounds.Min.Y, p.Y);
        bounds.Max.X = std::max(bounds.Max.X, p.X);
        bounds.Max.Y = std::max(bounds.Max.Y, p.Y);
    }
    return bounds;
}

std::vector<Vec2d> Rect(float u0, float v0, float u1, float v1)
{
    return CarveShapeOutline(CarveShape::Rectangle, Vec2d{ u0, v0 }, Vec2d{ u1, v1 }, {});
}

// `euler` is V - E + F: zero for a tunnel enclosed on all sides, which adds a
// handle, and two for a notch open to the outside, which does not.
void ExpectSoundTunnel(const BrushMesh& mesh, int euler)
{
    BrushMesh copy = mesh;
    const BrushRepairResult report = BrushValidateAndRepair(copy);
    EXPECT_TRUE(report.Ok);
    EXPECT_TRUE(report.Closed) << "the tunnel did not close the solid";
    EXPECT_FALSE(report.Changed) << "repair had to clean up after the tunnel";

    // A tunnel that failed to join its two openings would still be closed and
    // would still not balance.
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
            edges.insert({ std::min(a, b), std::max(a, b) });
        }
    const int v = static_cast<int>(mesh.Vertices.size());
    const int e = static_cast<int>(edges.size());
    const int f = static_cast<int>(mesh.Faces.size());
    EXPECT_EQ(v - e + f, euler) << "the pierced solid has the wrong topology";
}
}

TEST(CarveFacePolygonThrough, AWindowOpensATunnelWithOneWallPerEdge)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f),
        kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());

    EXPECT_TRUE(outcome.Value().CutFaces.empty()) << "a pierce leaves no face in the opening";
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), 4u);
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 4u) << "two bridged faces on each side";
    ExpectSoundTunnel(outcome.Value().Mesh, 0);
}

TEST(CarveFacePolygonThrough, AnArchTunnelsJustTheSame)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, Vec2d{ bounds.Min.X + 1.0f, bounds.Min.Y + 0.5f },
        Vec2d{ bounds.Max.X - 1.0f, bounds.Max.Y - 0.5f }, { 0.6f, 6 });

    const CarveOutcome outcome = CarveFacePolygonThrough(box, face, *frame.Frame, arch, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), arch.size());
    ExpectSoundTunnel(outcome.Value().Mesh, 0);
}

TEST(CarveFacePolygonThrough, TheWallsFaceIntoTheTunnel)
{
    // An inward-facing wall lights as if the doorway were solid and picks from
    // the wrong side, so the winding is worth pinning rather than trusting.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f),
        kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());

    const BrushMesh& carved = outcome.Value().Mesh;
    // The tunnel runs along the face normal through the middle of the box, so
    // every wall normal should point back at that axis.
    const Vec3d axis = BrushMeshCentroid(carved);
    for (std::uint32_t index : outcome.Value().TunnelWalls)
    {
        const Vec3d normal = BrushComputeFaceNormal(carved, carved.Faces[index]);
        const Vec3d center = BrushFaceCentroid(carved, carved.Faces[index]);
        Vec3d toAxis = axis - center;
        toAxis = toAxis - frame.Frame->Normal * toAxis.Dot(frame.Frame->Normal);
        EXPECT_GT(normal.Dot(toAxis), 0.0f) << "wall " << index << " faces into the solid";
    }
}

TEST(CarveFacePolygonThrough, APlaneGetsAHoleRatherThanARefusal)
{
    BrushMesh plane = BrushOps::MakePlane(Vec3d{ 2, 2, 2 }, 1, 1);
    ASSERT_EQ(plane.Faces.size(), 1u);
    const BrushFaceFrameResult frame = FaceFrame(plane, 0, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        plane, 0, *frame.Frame,
        Rect(bounds.Min.X + 0.5f, bounds.Min.Y + 0.5f, bounds.Max.X - 0.5f, bounds.Max.Y - 0.5f),
        kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_TRUE(outcome.Value().CutFaces.empty());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 2u);
    EXPECT_TRUE(outcome.Value().TunnelWalls.empty());
}

TEST(CarveFacePolygonThrough, ARefusalCarriesNoMeshAndSaysWhy)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outside = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Max.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X + 2.0f, bounds.Max.Y - 1.0f),
        kPlanarTol);
    EXPECT_FALSE(outside.Ok());
    EXPECT_EQ(outside.Status(), CarveStatus::OutsideHost);
}

TEST(CarveFacePolygonThrough, ADoorwayFlushWithTheFloorOpensANotch)
{
    // The flush case: no sill under the doorway, and no seams radiating from
    // its corners across the floor.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y, bounds.Max.X - 1.0f, bounds.Max.Y - 1.5f),
        kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());

    EXPECT_TRUE(outcome.Value().CutFaces.empty());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), 3u) << "the flush side opens instead of walling";
    ExpectSoundTunnel(outcome.Value().Mesh, 2);
}

TEST(CarveFacePolygonThrough, AnArchDoorwayFlushWithTheFloor)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, Vec2d{ bounds.Min.X + 1.0f, bounds.Min.Y },
        Vec2d{ bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f }, { 0.5f, 6 });

    const CarveOutcome outcome = CarveFacePolygonThrough(box, face, *frame.Frame, arch, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), arch.size() - 1) << "only the floor edge opens";
    ExpectSoundTunnel(outcome.Value().Mesh, 2);
}

TEST(CarveFacePolygonThrough, FlushWithTwoAdjacentSides)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X, bounds.Min.Y, bounds.Min.X + 2.0f, bounds.Min.Y + 2.0f), kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), 2u);
    ExpectSoundTunnel(outcome.Value().Mesh, 2);
}

TEST(CarveFacePolygonThrough, ASecondDoorwayFlushAgainstTheFirst)
{
    // Two doorways side by side, the second sharing the first's jamb. The side
    // the channel crosses has already been subdivided by the first carve, which
    // is the case the coplanar merge exists for.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 4, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome first = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 0.5f, bounds.Min.Y, bounds.Min.X + 1.5f, bounds.Min.Y + 2.0f),
        kPlanarTol);
    ASSERT_TRUE(first.Ok()) << CarveStatusText(first.Status());
    ExpectSoundTunnel(first.Value().Mesh, 2);

    const BrushMesh& carved = first.Value().Mesh;
    std::uint32_t host = 0xFFFFFFFFu;
    for (std::uint32_t index : first.Value().SurroundFaces)
        if (BrushComputeFaceNormal(carved, carved.Faces[index]).Dot(frame.Frame->Normal) > 0.99f)
        {
            host = index;
            break;
        }
    ASSERT_NE(host, 0xFFFFFFFFu);

    const BrushFaceFrameResult second = FaceFrame(carved, host, kPlanarTol);
    ASSERT_TRUE(second.Frame.has_value()) << CarveStatusText(second.Status);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        carved, host, *second.Frame,
        Rect(bounds.Min.X + 1.5f, bounds.Min.Y, bounds.Min.X + 2.5f, bounds.Min.Y + 2.0f),
        kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    ExpectSoundTunnel(outcome.Value().Mesh, 2);
}

TEST(CarveFacePolygonThrough, ASlotSpanningOppositeSidesDoesNotSplitTheBrush)
{
    // Flush on both the floor and the ceiling: the channel would cut the brush
    // into two pieces, which is not a carve.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y, bounds.Max.X - 1.0f, bounds.Max.Y), kPlanarTol);
    EXPECT_FALSE(outcome.Ok()) << "a carve that halves the brush should be refused";
}

namespace
{
// A truncated pyramid: the front face is much larger than the back, so a shape
// carved into the front projects past the back's edges.
BrushMesh MakeRectFrustum(float frontHalf, float backHalf)
{
    BrushMesh mesh;
    mesh.Vertices = {
        { { -frontHalf, -frontHalf, 1.0f } }, { { frontHalf, -frontHalf, 1.0f } },
        { { frontHalf, frontHalf, 1.0f } },   { { -frontHalf, frontHalf, 1.0f } },
        { { -backHalf, -backHalf, -1.0f } },  { { backHalf, -backHalf, -1.0f } },
        { { backHalf, backHalf, -1.0f } },    { { -backHalf, backHalf, -1.0f } },
    };
    mesh.Faces = {
        BrushFace{ .Loop = { 0, 1, 2, 3 } }, BrushFace{ .Loop = { 4, 7, 6, 5 } },
        BrushFace{ .Loop = { 0, 4, 5, 1 } }, BrushFace{ .Loop = { 1, 5, 6, 2 } },
        BrushFace{ .Loop = { 2, 6, 7, 3 } }, BrushFace{ .Loop = { 3, 7, 4, 0 } },
    };
    BrushValidateAndRepair(mesh);
    BrushOrientFacesOutward(mesh);
    return mesh;
}

// Every face's texture axes lie in its own plane. A wall inheriting the cap's
// projection would be edge-on to it, which smears the texture into streaks.
bool AllUvAxesInFacePlanes(const BrushMesh& mesh)
{
    for (const BrushFace& face : mesh.Faces)
    {
        const Vec3d normal = BrushComputeFaceNormal(mesh, face).Normalized();
        if (std::abs(face.Material.Uv.AxisU.Normalized().Dot(normal)) > 1e-3f)
            return false;
        if (std::abs(face.Material.Uv.AxisV.Normalized().Dot(normal)) > 1e-3f)
            return false;
    }
    return true;
}
}

TEST(CarveFacePolygonThrough, TunnelWallsGetTheirOwnTextureProjection)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome window = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f),
        kPlanarTol);
    ASSERT_TRUE(window.Ok()) << CarveStatusText(window.Status());
    EXPECT_TRUE(AllUvAxesInFacePlanes(window.Value().Mesh));

    const CarveOutcome doorway = CarveFacePolygonThrough(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y, bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f), kPlanarTol);
    ASSERT_TRUE(doorway.Ok()) << CarveStatusText(doorway.Status());
    EXPECT_TRUE(AllUvAxesInFacePlanes(doorway.Value().Mesh));
}

TEST(CarveFacePolygonThrough, RefusesWhenTheProjectionLeavesTheFarFace)
{
    const BrushMesh frustum = MakeRectFrustum(/*frontHalf*/ 1.25f, /*backHalf*/ 0.35f);
    const std::uint32_t front = FaceFacing(frustum, Vec3d{ 0, 0, 1 });
    ASSERT_NE(front, 0xFFFFFFFFu);
    const BrushFaceFrameResult frame = FaceFrame(frustum, front, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);
    const float width = bounds.Max.X - bounds.Min.X;
    const float height = bounds.Max.Y - bounds.Min.Y;

    const CarveOutcome outcome = CarveFacePolygonThrough(
        frustum, front, *frame.Frame,
        Rect(bounds.Min.X + width * 0.25f, bounds.Min.Y + height * 0.25f,
             bounds.Min.X + width * 0.75f, bounds.Min.Y + height * 0.75f),
        kPlanarTol);
    EXPECT_FALSE(outcome.Ok()) << "the shape does not fit on the face it would come out of";
}

TEST(CarveFacePolygonThrough, FlushWithThreeSidesTakesTheSlab)
{
    // The channel reaches three of the four rims, so what is left is the box
    // shrunk to the part the carve did not reach.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygonThrough(
        box, face, *frame.Frame, Rect(bounds.Min.X, bounds.Min.Y, bounds.Max.X, bounds.Min.Y + 2.0f),
        kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), 1u) << "only the fourth side still needs a wall";
    EXPECT_EQ(outcome.Value().Mesh.Faces.size(), 6u) << "what is left is a box again";
    EXPECT_EQ(outcome.Value().Mesh.Vertices.size(), 8u) << "the slab's corners went with it";
    ExpectSoundTunnel(outcome.Value().Mesh, 2);
}


TEST(CarveFacePolygonThrough, AWindowFlushAgainstALoopCutIsWalledNotNotched)
{
    // A loop cut leaves the wall as two coplanar faces sharing an edge. A
    // window carved flush against that edge lies on its host's rim, but the
    // face across the edge is the same wall: the surface continues, so the
    // edge gets a tunnel wall like any interior edge rather than a notch cut
    // through a coplanar neighbour, which cannot exist.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t wall = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const std::optional<BrushOps::BrushRectFaceFrame> quad = BrushOps::RectFaceFrame(box, wall);
    ASSERT_TRUE(quad.has_value());
    // One horizontal loop cut across the middle of the wall, at world y = 0
    // (the quad frame's U runs along world Y here; the other three bounds are
    // flush with the wall's own edges and cut nothing).
    const BrushMesh cut = BrushOps::InsertFaceLoopBounds(box, wall, Vec2d{ 0.0f, 0.0f },
                                                         Vec2d{ quad->Width * 0.5f, quad->Height });
    ASSERT_GT(cut.Faces.size(), box.Faces.size());

    // The upper of the two coplanar wall faces: its sill is the loop cut.
    std::uint32_t host = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < cut.Faces.size(); ++i)
        if (BrushComputeFaceNormal(cut, cut.Faces[i]).X > 0.99f
            && BrushFaceCentroid(cut, cut.Faces[i]).Dot(Vec3d::Up()) > 0.0f)
            host = i;
    ASSERT_NE(host, 0xFFFFFFFFu);
    const BrushFaceFrameResult frame = FaceFrame(cut, host, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const std::vector<Vec2d> window =
        Rect(bounds.Min.X + 0.5f, bounds.Min.Y, bounds.Max.X - 0.5f, bounds.Max.Y - 0.25f);
    const CarveOutcome outcome = CarveFacePolygonThrough(cut, host, *frame.Frame, window, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), window.size())
        << "the edge on the loop cut is a seam and keeps its wall";
    ExpectSoundTunnel(outcome.Value().Mesh, 0);
}

namespace
{
// Split `face` into two along the chord between two of its vertices, keeping
// the loop's winding on both halves. Returns the half containing `keep`.
std::uint32_t SplitFaceBetween(BrushMesh& mesh, std::uint32_t face, std::uint32_t a, std::uint32_t b,
                               std::uint32_t keep)
{
    const std::vector<std::uint32_t> loop = mesh.Faces[face].Loop;
    const std::size_t n = loop.size();
    const std::size_t ia = static_cast<std::size_t>(std::find(loop.begin(), loop.end(), a) - loop.begin());
    std::vector<std::uint32_t> rotated;
    for (std::size_t k = 0; k < n; ++k)
        rotated.push_back(loop[(ia + k) % n]);
    const std::size_t ib = static_cast<std::size_t>(std::find(rotated.begin(), rotated.end(), b) - rotated.begin());
    const std::vector<std::uint32_t> first(rotated.begin(), rotated.begin() + static_cast<std::ptrdiff_t>(ib) + 1);
    std::vector<std::uint32_t> second(rotated.begin() + static_cast<std::ptrdiff_t>(ib), rotated.end());
    second.push_back(rotated.front());
    mesh.Faces[face].Loop = first;
    BrushFace other = mesh.Faces[face];
    other.Loop = second;
    mesh.Faces.push_back(std::move(other));
    const std::uint32_t added = static_cast<std::uint32_t>(mesh.Faces.size() - 1);
    return std::find(first.begin(), first.end(), keep) != first.end() ? face : added;
}
}

TEST(CarveFacePolygonThrough, AHalfSeamChannelIsRefused)
{
    // The front face is split at y = -1, so a sill there is a seam on the near
    // end. The back face's own rim is at y = -1 and the slanted bottom bends
    // away from it, so the same sill is a notch on the far end. A wall on one
    // end and a notch on the other cannot close.
    BrushMesh frustum = MakeRectFrustum(2.0f, 1.0f);
    const std::uint32_t front = FaceFacing(frustum, Vec3d{ 0, 0, 1 });
    ASSERT_NE(front, 0xFFFFFFFFu);
    const auto vertexAt = [&](Vec3d p) {
        for (std::uint32_t i = 0; i < frustum.Vertices.size(); ++i)
            if ((frustum.Vertices[i].Position - p).SqrMagnitude() < 1e-8f)
                return i;
        return 0xFFFFFFFFu;
    };
    const std::uint32_t rightBottom = vertexAt({ 2, -2, 1 });
    const std::uint32_t rightTop = vertexAt({ 2, 2, 1 });
    const std::uint32_t leftBottom = vertexAt({ -2, -2, 1 });
    const std::uint32_t leftTop = vertexAt({ -2, 2, 1 });
    ASSERT_NE(rightTop, 0xFFFFFFFFu);
    std::optional<BrushOps::BrushEdgeSplit> right =
        BrushOps::InsertVertexOnEdge(frustum, rightBottom, rightTop, Vec3d{ 2, -1, 1 }, 1e-4f);
    ASSERT_TRUE(right.has_value());
    frustum = std::move(right->Mesh);
    std::optional<BrushOps::BrushEdgeSplit> left =
        BrushOps::InsertVertexOnEdge(frustum, leftBottom, leftTop, Vec3d{ -2, -1, 1 }, 1e-4f);
    ASSERT_TRUE(left.has_value());
    frustum = std::move(left->Mesh);
    const std::uint32_t upper = SplitFaceBetween(frustum, front, right->Vertex, left->Vertex, rightTop);
    ASSERT_TRUE(BrushValidateAndRepair(frustum).Ok);

    const BrushFaceFrameResult frame = FaceFrame(frustum, upper, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value()) << CarveStatusText(frame.Status);
    const Bounds bounds = OutlineBounds(*frame.Frame);
    const float middle = (bounds.Min.X + bounds.Max.X) * 0.5f;
    const CarveOutcome outcome = CarveFacePolygonThrough(
        frustum, upper, *frame.Frame,
        Rect(middle - 0.5f, bounds.Min.Y, middle + 0.5f, bounds.Min.Y + 1.5f), kPlanarTol);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::ChannelCrossesHole) << CarveStatusText(outcome.Status());
}

TEST(CarveFacePolygonThrough, AWholeLoopCellCanBeTunnelled)
{
    // The outline is the cell's own rim. There is nothing to leave around it,
    // and every edge has a coplanar neighbour, so all four are walled.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t wall = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const std::optional<BrushOps::BrushRectFaceFrame> quad = BrushOps::RectFaceFrame(box, wall);
    ASSERT_TRUE(quad.has_value());
    const Vec2d rectMin{ 1.0f, 1.0f };
    const Vec2d rectMax{ 3.0f, 3.0f };
    const BrushMesh cut = BrushOps::InsertFaceLoopBounds(box, wall, rectMin, rectMax);
    const std::optional<std::uint32_t> cell = BrushOps::FindRectFaceInFrame(cut, *quad, rectMin, rectMax);
    ASSERT_TRUE(cell.has_value());
    const BrushFaceFrameResult frame = FaceFrame(cut, *cell, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());

    const CarveOutcome outcome =
        CarveFacePolygonThrough(cut, *cell, *frame.Frame, frame.Frame->Outline, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_TRUE(outcome.Value().CutFaces.empty());
    EXPECT_TRUE(outcome.Value().SurroundFaces.empty());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), 4u);
    ExpectSoundTunnel(outcome.Value().Mesh, 0);
}

TEST(CarveFacePolygon, AFlatCarveOfTheWholeFaceStillRefuses)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t wall = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, wall, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const CarveOutcome outcome = CarveFacePolygon(box, wall, *frame.Frame, frame.Frame->Outline);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::InvalidOutline);
}
