#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"
#include "brush/CarveSurround.h"

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

std::vector<FaceCorners> Descriptors(const BrushMesh& mesh, const std::vector<std::uint32_t>& faces)
{
    std::vector<FaceCorners> out;
    for (std::uint32_t f : faces)
        out.push_back(CornersOf(mesh, f));
    return out;
}

// A box's +X wall split by a horizontal loop at world y = 0, picked on its
// lower half. The surface is both halves; the frame is the lower half's.
struct SplitWall
{
    BrushMesh Mesh;
    std::uint32_t Lower = 0;
    std::uint32_t Upper = 0;
    BrushFaceFrame Frame;
    std::vector<FaceCorners> Surface;
};

SplitWall MakeSplitWall()
{
    SplitWall wall;
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushOps::BrushRectFaceFrame quad = *BrushOps::RectFaceFrame(box, face);
    wall.Mesh = BrushOps::InsertFaceLoopBounds(box, face, Vec2d{ 0.0f, 0.0f },
                                               Vec2d{ quad.Width * 0.5f, quad.Height });
    for (std::uint32_t i = 0; i < wall.Mesh.Faces.size(); ++i)
    {
        if (BrushComputeFaceNormal(wall.Mesh, wall.Mesh.Faces[i]).X < 0.99f)
            continue;
        (BrushFaceCentroid(wall.Mesh, wall.Mesh.Faces[i]).Dot(Vec3d::Up()) > 0.0f ? wall.Upper : wall.Lower) = i;
    }
    wall.Frame = *FaceFrame(wall.Mesh, wall.Lower, kPlanarTol).Frame;
    wall.Surface = Descriptors(wall.Mesh, CoplanarSurface(wall.Mesh, wall.Lower, kPlanarTol));
    return wall;
}

// The seam between the halves is the lower face's top edge: its V max.
float SeamV(const SplitWall& wall)
{
    float v = wall.Frame.Outline.front().Y;
    for (const Vec2d& p : wall.Frame.Outline)
        v = std::max(v, p.Y);
    return v;
}

// The frame's origin is a loop vertex, so U is not centred on the face; the
// outlines below are placed about its middle.
float MidU(const SplitWall& wall)
{
    float lo = wall.Frame.Outline.front().X;
    float hi = lo;
    for (const Vec2d& p : wall.Frame.Outline)
    {
        lo = std::min(lo, p.X);
        hi = std::max(hi, p.X);
    }
    return (lo + hi) * 0.5f;
}

std::vector<Vec2d> Rect(float u0, float v0, float u1, float v1)
{
    return CarveShapeOutline(CarveShape::Rectangle, Vec2d{ u0, v0 }, Vec2d{ u1, v1 }, {});
}

int Euler(const BrushMesh& mesh)
{
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
            edges.insert({ std::min(a, b), std::max(a, b) });
        }
    return static_cast<int>(mesh.Vertices.size()) - static_cast<int>(edges.size())
        + static_cast<int>(mesh.Faces.size());
}

void ExpectSound(const BrushMesh& mesh)
{
    BrushMesh copy = mesh;
    const BrushRepairResult report = BrushValidateAndRepair(copy);
    EXPECT_TRUE(report.Ok);
    EXPECT_TRUE(report.Closed) << "the solid did not stay closed";
    EXPECT_FALSE(report.Changed) << "repair had to clean up after the carve";
}
}

TEST(CarveAcrossSurface, AnArchAcrossALoopCutBecomesTwoFacesSplitAtTheCut)
{
    const SplitWall wall = MakeSplitWall();
    ASSERT_EQ(wall.Surface.size(), 2u);
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, Vec2d{ mid - 1.0f, seam - 1.0f }, Vec2d{ mid + 1.0f, seam + 1.0f }, { 0.5f, 8 });

    // The plain kernel stops at the face.
    const CarveOutcome plain = CarveFacePolygon(wall.Mesh, wall.Lower, wall.Frame, arch);
    EXPECT_FALSE(plain.Ok());
    EXPECT_EQ(plain.Status(), CarveStatus::OutsideHost);

    const CarveOutcome outcome = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, arch, false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const CarveSuccess& carved = outcome.Value();
    EXPECT_EQ(carved.CutFaces.size(), 2u) << "one opening face per side of the cut";
    ExpectSound(carved.Mesh);
    EXPECT_EQ(Euler(carved.Mesh), 2);
    // The cut edge is still there outside the opening: both halves' rims still
    // meet at the seam on the wall's sides.
    int seamVertices = 0;
    for (const BrushVertex& v : carved.Mesh.Vertices)
        if (std::abs(v.Position.Dot(Vec3d::Up())) < 1e-4f && std::abs(v.Position.X - 2.0f) < 1e-4f
            && std::abs(std::abs(v.Position.Z) - 2.0f) < 1e-4f)
            ++seamVertices;
    EXPECT_EQ(seamVertices, 2);
}

TEST(CarveAcrossSurface, AnArchAcrossALoopCutPiercesAsOneTunnel)
{
    const SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, Vec2d{ mid - 1.0f, seam - 1.0f }, Vec2d{ mid + 1.0f, seam + 1.0f }, { 0.5f, 8 });
    const CarveOutcome outcome = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, arch, true, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_TRUE(outcome.Value().CutFaces.empty());
    EXPECT_EQ(outcome.Value().TunnelWalls.size(), arch.size()) << "no wall left standing at the seam";
    ExpectSound(outcome.Value().Mesh);
    EXPECT_EQ(Euler(outcome.Value().Mesh), 0);
}

TEST(CarveAcrossSurface, AShapeOverAGridCornerSpansFourFaces)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushOps::BrushRectFaceFrame quad = *BrushOps::RectFaceFrame(box, face);
    BrushMesh grid = BrushOps::InsertFaceLoopBounds(box, face, Vec2d{ 0.0f, 0.0f },
                                                    Vec2d{ quad.Width * 0.5f, quad.Height });
    const std::uint32_t half = FaceFacing(grid, Vec3d{ 1, 0, 0 });
    const BrushOps::BrushRectFaceFrame halfQuad = *BrushOps::RectFaceFrame(grid, half);
    grid = BrushOps::InsertFaceLoopBounds(grid, half, Vec2d{ 0.0f, 0.0f },
                                          Vec2d{ halfQuad.Width, halfQuad.Height * 0.5f });
    const std::uint32_t picked = FaceFacing(grid, Vec3d{ 1, 0, 0 });
    const BrushFaceFrame frame = *FaceFrame(grid, picked, kPlanarTol).Frame;
    const std::vector<std::uint32_t> surface = CoplanarSurface(grid, picked, kPlanarTol);
    ASSERT_EQ(surface.size(), 4u);
    // The grid's centre is world (2, 0, 0); a box around it in the frame.
    const Vec2d centre = frame.ToFrame(Vec3d{ 2.0f, 0.0f, 0.0f });
    const CarveOutcome outcome = CarveAcrossSurface(
        grid, Descriptors(grid, surface), frame,
        Rect(centre.X - 0.5f, centre.Y - 0.5f, centre.X + 0.5f, centre.Y + 0.5f), false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().CutFaces.size(), 4u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveAcrossSurface, AnLShapedSurfaceDoesNotCoverItsMissingQuadrant)
{
    // Four cells; delete one. The union's extent still contains the gap, and
    // a shape reaching into it is outside the surface, not inside its box.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushOps::BrushRectFaceFrame quad = *BrushOps::RectFaceFrame(box, face);
    BrushMesh grid = BrushOps::InsertFaceLoopBounds(box, face, Vec2d{ 0.0f, 0.0f },
                                                    Vec2d{ quad.Width * 0.5f, quad.Height });
    const std::uint32_t half = FaceFacing(grid, Vec3d{ 1, 0, 0 });
    const BrushOps::BrushRectFaceFrame halfQuad = *BrushOps::RectFaceFrame(grid, half);
    grid = BrushOps::InsertFaceLoopBounds(grid, half, Vec2d{ 0.0f, 0.0f },
                                          Vec2d{ halfQuad.Width, halfQuad.Height * 0.5f });
    std::uint32_t gone = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < grid.Faces.size(); ++i)
    {
        const Vec3d c = BrushFaceCentroid(grid, grid.Faces[i]);
        if (BrushComputeFaceNormal(grid, grid.Faces[i]).X > 0.99f && c.Y > 0.0f && c.Z > 0.0f)
            gone = i;
    }
    ASSERT_NE(gone, 0xFFFFFFFFu);
    grid = BrushOps::DeleteFace(grid, gone);
    const std::uint32_t picked = FaceFacing(grid, Vec3d{ 1, 0, 0 });
    const BrushFaceFrame frame = *FaceFrame(grid, picked, kPlanarTol).Frame;
    const std::vector<std::uint32_t> surface = CoplanarSurface(grid, picked, kPlanarTol);
    ASSERT_EQ(surface.size(), 3u);
    const Vec2d centre = frame.ToFrame(Vec3d{ 2.0f, 0.0f, 0.0f });
    const CarveOutcome outcome = CarveAcrossSurface(
        grid, Descriptors(grid, surface), frame,
        Rect(centre.X - 0.5f, centre.Y - 0.5f, centre.X + 0.5f, centre.Y + 0.5f), false, kPlanarTol);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::OutsideHost);
}

namespace
{
// The split wall with an arch already carved flat into its upper half, leaving
// one non-convex spandrel wrapped around the arc (the apex stops short of the
// face's top). Springline at seam + 0.5, semi-axes 1 (U) and 1 (V), so at
// |U| = 0.3 the arc is at seam + 1.45 and at |U| = 0.9 it is at seam + 0.94.
SplitWall MakeWallWithSpandrels()
{
    SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const BrushFaceFrame upper = *FaceFrame(wall.Mesh, wall.Upper, kPlanarTol).Frame;
    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, Vec2d{ mid - 1.0f, seam }, Vec2d{ mid + 1.0f, seam + 1.5f }, { 1.0f / 1.5f, 8 });
    std::vector<Vec2d> inUpper;
    for (const Vec2d& p : arch)
        inUpper.push_back(upper.ToFrame(wall.Frame.ToWorld(p)));
    const CarveOutcome carved = CarveFacePolygon(wall.Mesh, wall.Upper, upper, inUpper);
    if (!carved.Ok())
        return wall;
    wall.Mesh = carved.Value().Mesh;
    wall.Lower = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < wall.Mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(wall.Mesh, wall.Mesh.Faces[i]).X > 0.99f
            && BrushFaceCentroid(wall.Mesh, wall.Mesh.Faces[i]).Dot(Vec3d::Up()) < 0.0f)
            wall.Lower = i;
    wall.Frame = *FaceFrame(wall.Mesh, wall.Lower, kPlanarTol).Frame;
    wall.Surface = Descriptors(wall.Mesh, CoplanarSurface(wall.Mesh, wall.Lower, kPlanarTol));
    return wall;
}
}

TEST(CarveAcrossSurface, ABoxTouchingASpandrelsExtentButNotItsPolygonIsNotRefused)
{
    const SplitWall wall = MakeWallWithSpandrels();
    ASSERT_EQ(wall.Surface.size(), 3u) << "lower half, arch, spandrel";
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    // Extents overlap the spandrel; the polygon stays inside the arch.
    const CarveOutcome outcome = CarveAcrossSurface(
        wall.Mesh, wall.Surface, wall.Frame, Rect(mid - 0.3f, seam - 0.5f, mid + 0.3f, seam + 0.75f), false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().CutFaces.size(), 2u) << "the lower half and the arch";
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveAcrossSurface, AConvexShapeIsClippedByAConcaveFace)
{
    // The box reaches the lower half, the arch and the spandrel around it.
    // The spandrel is concave, the box is not: the box clips the spandrel.
    const SplitWall wall = MakeWallWithSpandrels();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const CarveOutcome outcome = CarveAcrossSurface(
        wall.Mesh, wall.Surface, wall.Frame, Rect(mid + 0.5f, seam - 0.5f, mid + 0.9f, seam + 1.4f), false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().CutFaces.size(), 3u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveAcrossSurface, AConcaveShapeOverAConcaveFaceIsRefused)
{
    // An L-shaped outline into the spandrel: neither side can clip the other.
    const SplitWall wall = MakeWallWithSpandrels();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const std::vector<Vec2d> ell = { { mid + 0.5f, seam - 0.5f }, { mid + 0.9f, seam - 0.5f },
                                     { mid + 0.9f, seam + 1.4f }, { mid + 0.7f, seam + 1.4f },
                                     { mid + 0.7f, seam + 0.2f }, { mid + 0.5f, seam + 0.2f } };
    const CarveOutcome outcome = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, ell, false, kPlanarTol);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::HostNotConvex);
}

TEST(CarveAcrossSurface, ABoxAcrossASeamIntoACarvedFace)
{
    // The upper half already has a pierced window; a box from the lower half
    // across the seam into what is left of the upper half, clear of the
    // window, is one carve in two faces.
    for (bool pierce : { false, true })
    {
        SplitWall wall = MakeSplitWall();
        const float seam = SeamV(wall);
        const float mid = MidU(wall);
        const BrushFaceFrame upper = *FaceFrame(wall.Mesh, wall.Upper, kPlanarTol).Frame;
        std::vector<Vec2d> window;
        for (const Vec2d& p : Rect(mid - 1.5f, seam + 0.5f, mid - 0.5f, seam + 1.5f))
            window.push_back(upper.ToFrame(wall.Frame.ToWorld(p)));
        const CarveOutcome first = CarveFacePolygonThrough(wall.Mesh, wall.Upper, upper, window, kPlanarTol);
        ASSERT_TRUE(first.Ok()) << CarveStatusText(first.Status());
        wall.Mesh = first.Value().Mesh;
        wall.Lower = 0xFFFFFFFFu;
        for (std::uint32_t i = 0; i < wall.Mesh.Faces.size(); ++i)
            if (BrushComputeFaceNormal(wall.Mesh, wall.Mesh.Faces[i]).X > 0.99f
                && BrushFaceCentroid(wall.Mesh, wall.Mesh.Faces[i]).Dot(Vec3d::Up()) < 0.0f)
                wall.Lower = i;
        ASSERT_NE(wall.Lower, 0xFFFFFFFFu);
        wall.Frame = *FaceFrame(wall.Mesh, wall.Lower, kPlanarTol).Frame;
        wall.Surface = Descriptors(wall.Mesh, CoplanarSurface(wall.Mesh, wall.Lower, kPlanarTol));
        const float seam2 = SeamV(wall);
        const float mid2 = MidU(wall);
        const std::vector<Vec2d> box = Rect(mid2 + 0.5f, seam2 - 1.0f, mid2 + 1.5f, seam2 + 1.0f);
        const CarveOutcome across = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, box, pierce, kPlanarTol);
        ASSERT_TRUE(across.Ok()) << CarveStatusText(across.Status());
        // The window's surround is two pieces joined by bridges, and the box
        // crosses one of those bridges: the lower half plus both pieces.
        EXPECT_EQ(across.Value().CutFaces.size(), pierce ? 0u : 3u);
        ExpectSound(across.Value().Mesh);
        EXPECT_EQ(Euler(across.Value().Mesh), pierce ? -2 : 0);
    }
}

TEST(CarveAcrossSurface, AShapeSpanningAcrossAHoleIsRefused)
{
    // The same wall, but the box straddles the window: its piece of the
    // upper half would be two parts, one each side of the hole.
    SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const BrushFaceFrame upper = *FaceFrame(wall.Mesh, wall.Upper, kPlanarTol).Frame;
    std::vector<Vec2d> window;
    for (const Vec2d& p : Rect(mid - 0.3f, seam + 0.5f, mid + 0.3f, seam + 1.5f))
        window.push_back(upper.ToFrame(wall.Frame.ToWorld(p)));
    const CarveOutcome first = CarveFacePolygonThrough(wall.Mesh, wall.Upper, upper, window, kPlanarTol);
    ASSERT_TRUE(first.Ok()) << CarveStatusText(first.Status());
    wall.Mesh = first.Value().Mesh;
    wall.Lower = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < wall.Mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(wall.Mesh, wall.Mesh.Faces[i]).X > 0.99f
            && BrushFaceCentroid(wall.Mesh, wall.Mesh.Faces[i]).Dot(Vec3d::Up()) < 0.0f)
            wall.Lower = i;
    wall.Frame = *FaceFrame(wall.Mesh, wall.Lower, kPlanarTol).Frame;
    wall.Surface = Descriptors(wall.Mesh, CoplanarSurface(wall.Mesh, wall.Lower, kPlanarTol));
    const float seam2 = SeamV(wall);
    const float mid2 = MidU(wall);
    const CarveOutcome across = CarveAcrossSurface(
        wall.Mesh, wall.Surface, wall.Frame, Rect(mid2 - 1.0f, seam2 - 0.5f, mid2 + 1.0f, seam2 + 1.0f), false, kPlanarTol);
    EXPECT_FALSE(across.Ok());
}

TEST(CarveAcrossSurface, AnEdgeOnTheSeamIsOneFaceWithSharedVertices)
{
    const SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const CarveOutcome outcome = CarveAcrossSurface(
        wall.Mesh, wall.Surface, wall.Frame, Rect(mid - 0.5f, seam - 1.0f, mid + 0.5f, seam), false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().CutFaces.size(), 1u) << "no zero-area piece on the far side of the seam";
    ExpectSound(outcome.Value().Mesh);
    // The two corners on the seam were inserted on the shared edge once: the
    // upper half now carries them too, as a hexagon.
    std::size_t upperCount = 0;
    for (const BrushFace& f : outcome.Value().Mesh.Faces)
        if (BrushComputeFaceNormal(outcome.Value().Mesh, f).X > 0.99f
            && BrushFaceCentroid(outcome.Value().Mesh, f).Dot(Vec3d::Up()) > 0.5f)
            upperCount = f.Loop.size();
    EXPECT_EQ(upperCount, 6u);
}

TEST(CarveAcrossSurface, WhollyInsideOneFaceIsThePlainCarve)
{
    const SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const std::vector<Vec2d> window = Rect(mid - 0.5f, seam - 1.5f, mid + 0.5f, seam - 0.5f);
    const CarveOutcome plain = CarveFacePolygon(wall.Mesh, wall.Lower, wall.Frame, window);
    const CarveOutcome across = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, window, false, kPlanarTol);
    ASSERT_TRUE(plain.Ok());
    ASSERT_TRUE(across.Ok()) << CarveStatusText(across.Status());
    EXPECT_EQ(across.Value().Mesh.Faces.size(), plain.Value().Mesh.Faces.size());
    EXPECT_EQ(across.Value().Mesh.Vertices.size(), plain.Value().Mesh.Vertices.size());
    EXPECT_EQ(across.Value().CutFaces, plain.Value().CutFaces);
}

TEST(CarveAcrossSurface, PastTheSurfacesOwnRimIsStillOutside)
{
    const SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const CarveOutcome outcome = CarveAcrossSurface(
        wall.Mesh, wall.Surface, wall.Frame, Rect(mid - 0.5f, seam - 3.0f, mid + 0.5f, seam - 1.0f), false, kPlanarTol);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::OutsideHost);
}

TEST(CoplanarSurface, StopsAtBentEdgesAndSkipsWhatIsNotConnected)
{
    const SplitWall wall = MakeSplitWall();
    EXPECT_EQ(CoplanarSurface(wall.Mesh, wall.Lower, kPlanarTol).size(), 2u);

    // Two boxes side by side in one mesh: their +X faces share a plane but no
    // edge, and one is not the other's surface.
    BrushMesh two = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const BrushMesh other = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const std::uint32_t base = static_cast<std::uint32_t>(two.Vertices.size());
    for (const BrushVertex& v : other.Vertices)
        two.Vertices.push_back(BrushVertex{ v.Position + Vec3d{ 0.0f, 0.0f, 3.0f } });
    for (const BrushFace& f : other.Faces)
    {
        BrushFace shifted = f;
        for (std::uint32_t& v : shifted.Loop)
            v += base;
        two.Faces.push_back(shifted);
    }
    EXPECT_EQ(CoplanarSurface(two, FaceFacing(two, Vec3d{ 1, 0, 0 }), kPlanarTol).size(), 1u);

    // A double-sided sheet: the reversed face shares every edge and the plane,
    // and faces the other way. Not the same surface.
    BrushMesh sheet = BrushOps::MakePlane(Vec3d{ 1, 1, 1 }, 1, 1);
    BrushFace back = sheet.Faces[0];
    std::reverse(back.Loop.begin(), back.Loop.end());
    sheet.Faces.push_back(back);
    EXPECT_EQ(CoplanarSurface(sheet, 0, kPlanarTol).size(), 1u);
}

TEST(CarveSurround, OverlapIsInteriorNotContact)
{
    using Poly = std::vector<Vec2d>;
    const Poly square = { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };
    const Poly touching = { { 2, 0 }, { 4, 0 }, { 4, 2 }, { 2, 2 } };
    const Poly corner = { { 2, 2 }, { 3, 2 }, { 3, 3 }, { 2, 3 } };
    const Poly crossing = { { 1, -1 }, { 3, -1 }, { 3, 1 }, { 1, 1 } };
    const Poly inside = { { 0.5f, 0.5f }, { 1, 0.5f }, { 1, 1 }, { 0.5f, 1 } };
    const Poly straddle = { { -1, 0.5f }, { 3, 0.5f }, { 3, 1 }, { -1, 1 } }; // no vertex inside, edges cross
    EXPECT_FALSE(PolygonsOverlap2D(square, touching, 1e-4f));
    EXPECT_FALSE(PolygonsOverlap2D(square, corner, 1e-4f));
    EXPECT_TRUE(PolygonsOverlap2D(square, crossing, 1e-4f));
    EXPECT_TRUE(PolygonsOverlap2D(square, inside, 1e-4f));
    EXPECT_TRUE(PolygonsOverlap2D(inside, square, 1e-4f));
    EXPECT_TRUE(PolygonsOverlap2D(square, straddle, 1e-4f));
    EXPECT_TRUE(PolygonsOverlap2D(square, square, 1e-4f)) << "coincident polygons share their whole interior";
}

TEST(CarveSurround, ConvexityAndConvexClip)
{
    using Poly = std::vector<Vec2d>;
    const Poly triangle = { { 0, 0 }, { 4, 0 }, { 0, 4 } };
    const Poly hexagon = { { 1, 0 }, { 3, 0 }, { 4, 2 }, { 3, 4 }, { 1, 4 }, { 0, 2 } };
    const Poly concave = { { 0, 0 }, { 4, 0 }, { 4, 4 }, { 2, 1 }, { 0, 4 } };
    const Poly collinear = { { 0, 0 }, { 2, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } };
    EXPECT_TRUE(IsConvexPolygon2D(triangle, 1e-4f));
    EXPECT_TRUE(IsConvexPolygon2D(hexagon, 1e-4f));
    EXPECT_FALSE(IsConvexPolygon2D(concave, 1e-4f));
    EXPECT_TRUE(IsConvexPolygon2D(collinear, 1e-4f));

    const Poly square = { { 1, 1 }, { 3, 1 }, { 3, 3 }, { 1, 3 } };
    const Poly inTriangle = ClipPolygonToConvex2D(square, triangle, 1e-4f);
    EXPECT_NEAR(std::abs(PolygonSignedArea(inTriangle)), 2.0f, 1e-4f); // the square minus its cut corner
    const Poly inHexagon = ClipPolygonToConvex2D(square, hexagon, 1e-4f);
    EXPECT_NEAR(std::abs(PolygonSignedArea(inHexagon)), 4.0f, 1e-4f); // wholly inside
    const Poly wide = { { -1, 1 }, { 5, 1 }, { 5, 3 }, { -1, 3 } };
    const Poly band = ClipPolygonToConvex2D(wide, hexagon, 1e-4f);
    EXPECT_NEAR(std::abs(PolygonSignedArea(band)), 7.0f, 1e-3f); // the hexagon's middle band
}

namespace
{
// A wall with one window already carved (flat or pierced), then the surface
// gathered the way CaptureFace does: from a surround piece, by corners.
struct CarvedWall
{
    BrushMesh Mesh;
    std::uint32_t Surround = 0; // a concave piece the first window left
    BrushFaceFrame Frame;
    std::vector<FaceCorners> Surface;
};

CarvedWall MakeCarvedWall(bool pierced)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrame frame = *FaceFrame(box, face, kPlanarTol).Frame;
    float lo = frame.Outline.front().X, hi = lo, bottom = frame.Outline.front().Y, top = bottom;
    for (const Vec2d& p : frame.Outline)
    {
        lo = std::min(lo, p.X); hi = std::max(hi, p.X);
        bottom = std::min(bottom, p.Y); top = std::max(top, p.Y);
    }
    // A window in the left third of the wall.
    const std::vector<Vec2d> window = Rect(lo + 0.5f, bottom + 1.5f, lo + 1.5f, bottom + 2.5f);
    const CarveOutcome first = pierced ? CarveFacePolygonThrough(box, face, frame, window, kPlanarTol)
                                       : CarveFacePolygon(box, face, frame, window);
    CarvedWall wall;
    if (!first.Ok())
        return wall;
    wall.Mesh = first.Value().Mesh;
    wall.Surround = 0xFFFFFFFFu;
    for (std::uint32_t index : first.Value().SurroundFaces)
        if (BrushComputeFaceNormal(wall.Mesh, wall.Mesh.Faces[index]).X > 0.99f
            && !IsConvexPolygon2D(FaceFrame(wall.Mesh, index, kPlanarTol).Frame->Outline, 1e-4f))
        {
            wall.Surround = index;
            break;
        }
    if (wall.Surround == 0xFFFFFFFFu)
        return wall;
    wall.Frame = *FaceFrame(wall.Mesh, wall.Surround, kPlanarTol).Frame;
    wall.Surface = Descriptors(wall.Mesh, CoplanarSurface(wall.Mesh, wall.Surround, kPlanarTol));
    return wall;
}

// A second box in the right third of the wall, at the first window's height,
// in the surround's frame. The extent is the whole surface's, not the piece's:
// the piece's own bounds stop at the bridge the first carve drew.
std::vector<Vec2d> SecondWindow(const CarvedWall& wall)
{
    const Vec2d first = wall.Frame.ToFrame(wall.Surface.front().Corners.front());
    float lo = first.X, hi = first.X, bottom = first.Y;
    for (const FaceCorners& face : wall.Surface)
        for (const Vec3d& corner : face.Corners)
        {
            const Vec2d p = wall.Frame.ToFrame(corner);
            lo = std::min(lo, p.X); hi = std::max(hi, p.X);
            bottom = std::min(bottom, p.Y);
        }
    return Rect(hi - 1.5f, bottom + 1.5f, hi - 0.5f, bottom + 2.5f);
}
}

TEST(CarveAcrossSurface, ASecondCarveInsideAConcaveSurroundIsADirectCarve)
{
    const CarvedWall wall = MakeCarvedWall(false);
    ASSERT_NE(wall.Surround, 0xFFFFFFFFu) << "the first window left no concave surround piece";
    const std::vector<Vec2d> second = SecondWindow(wall);

    const CarveOutcome plain = CarveFacePolygon(wall.Mesh, wall.Surround, wall.Frame, second);
    ASSERT_TRUE(plain.Ok()) << CarveStatusText(plain.Status());
    const CarveOutcome across = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, second, false, kPlanarTol);
    ASSERT_TRUE(across.Ok()) << CarveStatusText(across.Status());
    EXPECT_EQ(across.Value().CutFaces, plain.Value().CutFaces);
    EXPECT_EQ(across.Value().SurroundFaces.size(), plain.Value().SurroundFaces.size());
    EXPECT_EQ(across.Value().Mesh.Faces.size(), plain.Value().Mesh.Faces.size());
    EXPECT_EQ(across.Value().Mesh.Vertices.size(), plain.Value().Mesh.Vertices.size());
    ExpectSound(across.Value().Mesh);
    EXPECT_EQ(Euler(across.Value().Mesh), 2);
}

TEST(CarveAcrossSurface, ASecondPierceInsideAConcaveSurroundAddsAHandle)
{
    const CarvedWall wall = MakeCarvedWall(true);
    ASSERT_NE(wall.Surround, 0xFFFFFFFFu);
    EXPECT_EQ(Euler(wall.Mesh), 0) << "the first pierce is one handle";
    const std::vector<Vec2d> second = SecondWindow(wall);
    const CarveOutcome across = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, second, true, kPlanarTol);
    ASSERT_TRUE(across.Ok()) << CarveStatusText(across.Status());
    EXPECT_TRUE(across.Value().CutFaces.empty());
    EXPECT_EQ(across.Value().TunnelWalls.size(), second.size());
    ExpectSound(across.Value().Mesh);
    EXPECT_EQ(Euler(across.Value().Mesh), -2) << "the second pierce is a second handle";
}

TEST(CarveAcrossSurface, TouchingANonConvexRegionsRimIsNotReachingIt)
{
    // Spanning the lower half and the arch, with the top vertex on the arc: the
    // spandrel is touched along its boundary, and needs no piece.
    const SplitWall wall = MakeWallWithSpandrels();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    // At U = mid the arc's apex is at seam + 1.5.
    const std::vector<Vec2d> outline = { { mid - 0.3f, seam - 0.5f }, { mid + 0.3f, seam - 0.5f },
                                         { mid, seam + 1.5f } };
    const CarveOutcome outcome = CarveAcrossSurface(wall.Mesh, wall.Surface, wall.Frame, outline, false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().CutFaces.size(), 2u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveAcrossSurface, ASecondCarveSpanningIntoAConvexNeighbourStillDistributes)
{
    // The split wall's lower half carved once; a second box from what is left
    // of it across the seam into the upper half.
    SplitWall wall = MakeSplitWall();
    const float seam = SeamV(wall);
    const float mid = MidU(wall);
    const CarveOutcome first = CarveFacePolygon(wall.Mesh, wall.Lower, wall.Frame,
                                                Rect(mid - 1.5f, seam - 1.5f, mid - 0.5f, seam - 0.5f));
    ASSERT_TRUE(first.Ok()) << CarveStatusText(first.Status());
    wall.Mesh = first.Value().Mesh;
    ASSERT_EQ(first.Value().SurroundFaces.size(), 2u);
    wall.Lower = first.Value().SurroundFaces.front();
    wall.Frame = *FaceFrame(wall.Mesh, wall.Lower, kPlanarTol).Frame;
    wall.Surface = Descriptors(wall.Mesh, CoplanarSurface(wall.Mesh, wall.Lower, kPlanarTol));
    const float seam2 = SeamV(wall);
    const float mid2 = MidU(wall);
    const CarveOutcome across = CarveAcrossSurface(
        wall.Mesh, wall.Surface, wall.Frame, Rect(mid2 + 0.5f, seam2 - 0.5f, mid2 + 1.0f, seam2 + 0.5f), false,
        kPlanarTol);
    ASSERT_TRUE(across.Ok()) << CarveStatusText(across.Status());
    EXPECT_EQ(across.Value().CutFaces.size(), 2u);
    ExpectSound(across.Value().Mesh);
}
