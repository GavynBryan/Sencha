#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace
{
constexpr float kPlanarTol = 1e-3f;
constexpr float kPointTol = 1e-4f;

std::uint32_t FaceFacing(const BrushMesh& mesh, Vec3d direction)
{
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(mesh, mesh.Faces[i]).Dot(direction) > 0.99f)
            return i;
    return 0xFFFFFFFFu;
}

// A box's +X wall, with both of the frames Edge Loop mode works in: the quad
// frame the loop cuts are authored in and the canonical frame the shape is.
struct LoopHost
{
    BrushMesh Mesh;
    std::uint32_t Face = 0;
    BrushOps::BrushRectFaceFrame Quad;
    BrushFaceFrame Frame;
};

LoopHost MakeLoopHost()
{
    LoopHost host;
    host.Mesh = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    host.Face = FaceFacing(host.Mesh, Vec3d{ 1, 0, 0 });
    host.Quad = *BrushOps::RectFaceFrame(host.Mesh, host.Face);
    host.Frame = *FaceFrame(host.Mesh, host.Face, kPlanarTol).Frame;
    return host;
}

struct Bounds
{
    Vec2d Min, Max;
};

// The box given in the quad frame, as the canonical frame sees it.
Bounds CanonicalBox(const LoopHost& host, Vec2d rectMin, Vec2d rectMax)
{
    const Vec2d corners[4] = { rectMin, { rectMax.X, rectMin.Y }, rectMax, { rectMin.X, rectMax.Y } };
    Bounds bounds{};
    for (int i = 0; i < 4; ++i)
    {
        const Vec3d world = host.Quad.Origin + host.Quad.AxisU * corners[i].X + host.Quad.AxisV * corners[i].Y;
        const Vec2d uv = host.Frame.ToFrame(world);
        if (i == 0)
        {
            bounds.Min = bounds.Max = uv;
            continue;
        }
        bounds.Min.X = std::min(bounds.Min.X, uv.X);
        bounds.Min.Y = std::min(bounds.Min.Y, uv.Y);
        bounds.Max.X = std::max(bounds.Max.X, uv.X);
        bounds.Max.Y = std::max(bounds.Max.Y, uv.Y);
    }
    return bounds;
}

std::vector<Vec2d> ShapeOver(const LoopHost& host, CarveShape shape, Vec2d rectMin, Vec2d rectMax,
                             const CarveShapeParams& params = {})
{
    const Bounds box = CanonicalBox(host, rectMin, rectMax);
    return CarveShapeOutline(shape, box.Min, box.Max, params);
}

CarveOutcome Carve(const LoopHost& host, CarveShape shape, Vec2d rectMin, Vec2d rectMax, bool pierce,
                   const CarveShapeParams& params = {})
{
    return CarveWithinLoopBounds(host.Mesh, host.Face, host.Quad, rectMin, rectMax, host.Frame,
                                 ShapeOver(host, shape, rectMin, rectMax, params), pierce, kPlanarTol);
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

bool SamePoint(Vec3d a, Vec3d b)
{
    return (a - b).SqrMagnitude() <= kPointTol * kPointTol;
}

std::vector<Vec3d> Corners(const BrushMesh& mesh, std::uint32_t face)
{
    std::vector<Vec3d> corners;
    for (std::uint32_t v : mesh.Faces[face].Loop)
        corners.push_back(mesh.Vertices[v].Position);
    return corners;
}

// Whether `mesh` has a face with exactly these corners in this cyclic order
// and nothing else: the rings carry every vertex a laid-in shape puts on the
// rim, so no face outside the carve gains one.
bool FaceSurvives(const BrushMesh& mesh, const std::vector<Vec3d>& corners)
{
    const std::size_t m = corners.size();
    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t start = 0; start < n; ++start)
        {
            if (!SamePoint(mesh.Vertices[face.Loop[start]].Position, corners[0]))
                continue;
            if (n != m)
                break;
            bool same = true;
            for (std::size_t j = 1; j < n && same; ++j)
                same = SamePoint(mesh.Vertices[face.Loop[(start + j) % n]].Position, corners[j]);
            if (same)
                return true;
        }
    }
    return false;
}

// Every face the loop cuts alone produce, other than the cell at the box (and
// the cell opposite it for a pierce), is still there with its corners in order;
// every vertex they minted still exists.
void ExpectLoopsSurvive(const LoopHost& host, const BrushMesh& control, const BrushMesh& result,
                        Vec2d rectMin, Vec2d rectMax, bool pierced)
{
    const Vec3d normal = host.Quad.AxisU.Cross(host.Quad.AxisV);
    for (std::uint32_t f = 0; f < control.Faces.size(); ++f)
    {
        const Vec3d rel = BrushFaceCentroid(control, control.Faces[f]) - host.Quad.Origin;
        const Vec2d uv{ rel.Dot(host.Quad.AxisU), rel.Dot(host.Quad.AxisV) };
        const bool inBox = uv.X > rectMin.X && uv.X < rectMax.X && uv.Y > rectMin.Y && uv.Y < rectMax.Y;
        const float facing = BrushComputeFaceNormal(control, control.Faces[f]).Dot(normal);
        if (inBox && (facing > 0.9f || (pierced && facing < -0.9f)))
            continue;
        EXPECT_TRUE(FaceSurvives(result, Corners(control, f))) << "control face " << f << " did not survive";
    }
    for (const BrushVertex& vertex : control.Vertices)
    {
        // A pierce removes the opening, and with it the ring crossings that lay
        // strictly inside the box on either face; everything on the rim stays.
        const Vec3d rel = vertex.Position - host.Quad.Origin;
        const Vec2d uv{ rel.Dot(host.Quad.AxisU), rel.Dot(host.Quad.AxisV) };
        const bool insideBox = uv.X > rectMin.X + kPointTol && uv.X < rectMax.X - kPointTol
            && uv.Y > rectMin.Y + kPointTol && uv.Y < rectMax.Y - kPointTol;
        if (pierced && insideBox)
            continue;
        const bool present = std::any_of(result.Vertices.begin(), result.Vertices.end(),
                                         [&](const BrushVertex& v) { return SamePoint(v.Position, vertex.Position); });
        EXPECT_TRUE(present) << "a loop-minted vertex went missing";
    }
}

// An interior box on the wall: every side is a loop cut, none is flush.
const Vec2d kInteriorMin{ 1.0f, 1.0f };
const Vec2d kInteriorMax{ 3.0f, 3.0f };

// The loops a shape in the box should leave, cut by the loop kernels alone:
// the box loops and one ring through every outline vertex on the box rim that
// is not a corner, found the way the kernel finds them.
BrushMesh LoopControl(const LoopHost& host, CarveShape shape, Vec2d rectMin, Vec2d rectMax,
                      const CarveShapeParams& params = {})
{
    BrushMesh control = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, rectMin, rectMax);
    const Bounds box = CanonicalBox(host, rectMin, rectMax);
    const auto onSideX = [&](Vec2d p) { return std::abs(p.X - box.Min.X) < 1e-4f || std::abs(p.X - box.Max.X) < 1e-4f; };
    const auto onSideY = [&](Vec2d p) { return std::abs(p.Y - box.Min.Y) < 1e-4f || std::abs(p.Y - box.Max.Y) < 1e-4f; };
    for (const Vec2d& p : CarveShapeOutline(shape, box.Min, box.Max, params))
    {
        if (onSideX(p) == onSideY(p))
            continue; // off the rim, or a corner
        const Vec3d rel = host.Frame.ToWorld(p) - host.Quad.Origin;
        const Vec2d uv{ rel.Dot(host.Quad.AxisU), rel.Dot(host.Quad.AxisV) };
        const bool sideIsConstantU = std::abs(uv.X - rectMin.X) < 1e-4f || std::abs(uv.X - rectMax.X) < 1e-4f;
        const std::optional<BrushMesh> ring =
            BrushOps::InsertFrameLoop(control, host.Quad, /*cutAlongU*/ !sideIsConstantU, sideIsConstantU ? uv.Y : uv.X);
        if (ring.has_value())
            control = *ring;
    }
    return control;
}
}

TEST(CarveWithinLoopBounds, AnArchIsLaidIntoTheCellTheLoopsLeave)
{
    const LoopHost host = MakeLoopHost();
    const BrushMesh control = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, kInteriorMin, kInteriorMax);
    ASSERT_GT(control.Faces.size(), host.Mesh.Faces.size());
    ASSERT_TRUE(BrushOps::FindRectFaceInFrame(control, host.Quad, kInteriorMin, kInteriorMax).has_value())
        << "the loops alone leave the box as one quad cell";

    const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, false);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const CarveSuccess& carved = outcome.Value();
    // The springline ring and the apex ring cut the arch in four: two quads
    // under the springline, which are loop cells as they stand, and the two
    // halves of the arc on their chords.
    ASSERT_EQ(carved.CutFaces.size(), 4u);
    const CarveShapeParams params;
    std::vector<std::size_t> sizes;
    for (std::uint32_t index : carved.CutFaces)
        sizes.push_back(carved.Mesh.Faces[index].Loop.size());
    std::sort(sizes.begin(), sizes.end());
    EXPECT_EQ(sizes[0], 4u);
    EXPECT_EQ(sizes[1], 4u);
    EXPECT_EQ(sizes[2], static_cast<std::size_t>(params.ArchSegments) / 2 + 2);
    EXPECT_EQ(sizes[3], static_cast<std::size_t>(params.ArchSegments) / 2 + 2);
    EXPECT_GT(carved.Mesh.Faces.size(), control.Faces.size()) << "the loop grid and the arch, not one or the other";
    EXPECT_FALSE(BrushOps::FindRectFaceInFrame(carved.Mesh, host.Quad, kInteriorMin, kInteriorMax).has_value())
        << "the cell is no longer one quad";
}

TEST(CarveWithinLoopBounds, ARectangleIsTheLoopCutsAlone)
{
    const LoopHost host = MakeLoopHost();
    const BrushMesh loops = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, kInteriorMin, kInteriorMax);
    const CarveOutcome flat = Carve(host, CarveShape::Rectangle, kInteriorMin, kInteriorMax, false);
    ASSERT_TRUE(flat.Ok()) << CarveStatusText(flat.Status());
    EXPECT_TRUE(flat.Value().CutFaces.empty());
    EXPECT_EQ(flat.Value().Mesh.Faces.size(), loops.Faces.size());
    EXPECT_EQ(flat.Value().Mesh.Vertices.size(), loops.Vertices.size());

    const BrushMesh tunnel = BrushOps::InsertFaceLoopBoundsThrough(host.Mesh, host.Face, kInteriorMin, kInteriorMax);
    const CarveOutcome pierced = Carve(host, CarveShape::Rectangle, kInteriorMin, kInteriorMax, true);
    ASSERT_TRUE(pierced.Ok()) << CarveStatusText(pierced.Status());
    EXPECT_TRUE(pierced.Value().CutFaces.empty());
    EXPECT_EQ(pierced.Value().Mesh.Faces.size(), tunnel.Faces.size());
    EXPECT_EQ(pierced.Value().Mesh.Vertices.size(), tunnel.Vertices.size());
    EXPECT_EQ(Euler(pierced.Value().Mesh), 0);
}

TEST(CarveWithinLoopBounds, AFlatArchLeavesOneSpandrelAndKeepsTheMaterial)
{
    LoopHost host = MakeLoopHost();
    host.Mesh.Faces[host.Face].Material.Uv.Rotation = 22.0f;
    const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, false);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const CarveSuccess& carved = outcome.Value();
    ASSERT_EQ(carved.CutFaces.size(), 4u);
    // The arch is inscribed in the cell: it runs along the sill and both jambs
    // and its apex touches the top edge, so what is left of the cell is the
    // two spandrels either side of the apex, one in each upper cell.
    EXPECT_EQ(carved.SurroundFaces.size(), 2u);
    EXPECT_TRUE(carved.TunnelWalls.empty());
    ExpectSound(carved.Mesh);
    // The two lower opening faces between them hold the box's sill corners.
    const Bounds box = CanonicalBox(host, kInteriorMin, kInteriorMax);
    int sillCorners = 0;
    for (std::uint32_t index : carved.CutFaces)
    {
        const std::vector<std::uint32_t>& loop = carved.Mesh.Faces[index].Loop;
        if (loop.size() != 4)
            continue;
        for (std::uint32_t v : loop)
        {
            const Vec2d uv = host.Frame.ToFrame(carved.Mesh.Vertices[v].Position);
            if (std::abs(uv.Y - box.Min.Y) < 1e-3f && (std::abs(uv.X - box.Min.X) < 1e-3f || std::abs(uv.X - box.Max.X) < 1e-3f))
                ++sillCorners;
        }
    }
    EXPECT_EQ(sillCorners, 2);
    for (std::uint32_t index : carved.CutFaces)
        EXPECT_FLOAT_EQ(carved.Mesh.Faces[index].Material.Uv.Rotation, 22.0f);
    for (std::uint32_t index : carved.SurroundFaces)
        EXPECT_FLOAT_EQ(carved.Mesh.Faces[index].Material.Uv.Rotation, 22.0f);
}

TEST(CarveWithinLoopBounds, APiercedArchInTheWallsInteriorWallsItsSill)
{
    const LoopHost host = MakeLoopHost();
    const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, true);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const CarveSuccess& carved = outcome.Value();
    EXPECT_TRUE(carved.CutFaces.empty());
    // The sill lies on a loop cut, and the cell below is the same wall: a seam,
    // walled like every other edge. The apex ring splits the sill in two.
    const std::vector<Vec2d> arch = ShapeOver(host, CarveShape::Arch, kInteriorMin, kInteriorMax);
    EXPECT_EQ(carved.TunnelWalls.size(), arch.size() + 1);
    ExpectSound(carved.Mesh);
    EXPECT_EQ(Euler(carved.Mesh), 0);
}

TEST(CarveWithinLoopBounds, APiercedArchFlushWithTheFloorOpensANotch)
{
    // Quad U runs along world Y on this wall, so U = 0 is the floor edge.
    const LoopHost host = MakeLoopHost();
    const Vec2d rectMin{ 0.0f, 1.0f };
    const Vec2d rectMax{ 2.0f, 3.0f };
    const CarveOutcome outcome = Carve(host, CarveShape::Arch, rectMin, rectMax, true);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const CarveSuccess& carved = outcome.Value();
    // The sill is on the brush's own rim and the floor bends away from it:
    // that edge opens a notch and has no wall (the apex ring splits the sill,
    // so two notches and two fewer walls than edges).
    const std::vector<Vec2d> arch = ShapeOver(host, CarveShape::Arch, rectMin, rectMax);
    EXPECT_EQ(carved.TunnelWalls.size(), arch.size() - 1);
    ExpectSound(carved.Mesh);
    EXPECT_EQ(Euler(carved.Mesh), 2);
}

TEST(CarveWithinLoopBounds, TheLoopsSurviveAFlatArch)
{
    const LoopHost host = MakeLoopHost();
    const BrushMesh control = LoopControl(host, CarveShape::Arch, kInteriorMin, kInteriorMax);
    const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, false);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    ExpectLoopsSurvive(host, control, outcome.Value().Mesh, kInteriorMin, kInteriorMax, false);
}

TEST(CarveWithinLoopBounds, TheLoopsSurviveAPiercedArch)
{
    // The far grid is the one a through carve could quietly retire or merge
    // while the front still looked right.
    const LoopHost host = MakeLoopHost();
    const BrushMesh control = LoopControl(host, CarveShape::Arch, kInteriorMin, kInteriorMax);
    const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, true);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    ExpectLoopsSurvive(host, control, outcome.Value().Mesh, kInteriorMin, kInteriorMax, true);
}

TEST(CarveWithinLoopBounds, AQuarterTurnedRectangleStillFillsTheCell)
{
    const LoopHost host = MakeLoopHost();
    const BrushMesh loops = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, kInteriorMin, kInteriorMax);
    CarveShapeParams params;
    params.Orientation = std::numbers::pi_v<float> * 0.5f;
    const CarveOutcome outcome = Carve(host, CarveShape::Rectangle, kInteriorMin, kInteriorMax, false, params);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_TRUE(outcome.Value().CutFaces.empty());
    EXPECT_EQ(outcome.Value().Mesh.Faces.size(), loops.Faces.size());
    EXPECT_EQ(outcome.Value().Mesh.Vertices.size(), loops.Vertices.size());
}

TEST(CarveWithinLoopBounds, AnEighthTurnedRectangleIsLaidInAsADiamond)
{
    const LoopHost host = MakeLoopHost();
    CarveShapeParams params;
    params.Orientation = std::numbers::pi_v<float> * 0.25f;
    const CarveOutcome outcome = Carve(host, CarveShape::Rectangle, kInteriorMin, kInteriorMax, false, params);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const CarveSuccess& carved = outcome.Value();
    // Its four points each get a ring, so the diamond is four triangles in
    // four cells, each leaving that cell's corner triangle.
    ASSERT_EQ(carved.CutFaces.size(), 4u);
    for (std::uint32_t index : carved.CutFaces)
        EXPECT_EQ(carved.Mesh.Faces[index].Loop.size(), 3u);
    EXPECT_EQ(carved.SurroundFaces.size(), 4u);
    ExpectSound(carved.Mesh);
}

TEST(CarveWithinLoopBounds, AThinSurroundIsLaidInRatherThanAbsorbed)
{
    // Inset every side by ten snap tolerances: the same area to any relative
    // tolerance worth having, but not the same region. Strictly inside, so the
    // surround is a ring bridged into two pieces.
    const LoopHost host = MakeLoopHost();
    Bounds box = CanonicalBox(host, kInteriorMin, kInteriorMax);
    const float inset = kCarveSnapTolerance * 10.0f;
    box.Min = Vec2d{ box.Min.X + inset, box.Min.Y + inset };
    box.Max = Vec2d{ box.Max.X - inset, box.Max.Y - inset };
    const std::vector<Vec2d> outline = CarveShapeOutline(CarveShape::Rectangle, box.Min, box.Max, {});
    const CarveOutcome outcome = CarveWithinLoopBounds(host.Mesh, host.Face, host.Quad, kInteriorMin,
                                                       kInteriorMax, host.Frame, outline, false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().CutFaces.size(), 1u);
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 2u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveWithinLoopBounds, ADepartureTheRingCannotReachRefuses)
{
    // The outline runs along the sill and leaves it ten snap tolerances short
    // of the corner. That asks for a ring that close to the box loop, which the
    // loop kernel cannot place (it clamps its cut away from the edge's ends),
    // and a ring somewhere else is not the loop the shape needs: refused.
    const LoopHost host = MakeLoopHost();
    Bounds box = CanonicalBox(host, kInteriorMin, kInteriorMax);
    box.Max.X -= kCarveSnapTolerance * 10.0f;
    const std::vector<Vec2d> outline = CarveShapeOutline(CarveShape::Rectangle, box.Min, box.Max, {});
    const CarveOutcome outcome = CarveWithinLoopBounds(host.Mesh, host.Face, host.Quad, kInteriorMin,
                                                       kInteriorMax, host.Frame, outline, false, kPlanarTol);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::TopologyFailure);
}

TEST(CarveWithinLoopBounds, ACollinearVertexOnTheRimStillFillsTheCell)
{
    const LoopHost host = MakeLoopHost();
    const BrushMesh loops = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, kInteriorMin, kInteriorMax);
    std::vector<Vec2d> outline = ShapeOver(host, CarveShape::Rectangle, kInteriorMin, kInteriorMax);
    ASSERT_EQ(outline.size(), 4u);
    const Vec2d middle{ (outline[0].X + outline[1].X) * 0.5f, (outline[0].Y + outline[1].Y) * 0.5f };
    outline.insert(outline.begin() + 1, middle);
    const CarveOutcome outcome = CarveWithinLoopBounds(host.Mesh, host.Face, host.Quad, kInteriorMin,
                                                       kInteriorMax, host.Frame, outline, false, kPlanarTol);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_TRUE(outcome.Value().CutFaces.empty());
    EXPECT_EQ(outcome.Value().Mesh.Faces.size(), loops.Faces.size());
    EXPECT_EQ(outcome.Value().Mesh.Vertices.size(), loops.Vertices.size());
}

TEST(CarveWithinLoopBounds, ANonQuadHostRefusesAtTheLoopStep)
{
    const BrushMesh cylinder = BrushOps::MakeCylinder(Vec3d{ 1.0f, 1.0f, 1.0f }, 2, 8);
    std::uint32_t cap = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < cylinder.Faces.size(); ++i)
        if (cylinder.Faces[i].Loop.size() > 4)
            cap = i;
    ASSERT_NE(cap, 0xFFFFFFFFu);
    const BrushFaceFrameResult frame = FaceFrame(cylinder, cap, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    // No quad frame exists for a cap; the tool never gets this far. Give the
    // kernel the canonical frame's axes as one and it still has to refuse.
    const BrushOps::BrushRectFaceFrame quad{ .Origin = frame.Frame->Origin, .AxisU = frame.Frame->AxisU,
                                             .AxisV = frame.Frame->AxisV, .Width = 2.0f, .Height = 2.0f };
    const std::vector<Vec2d> outline = CarveShapeOutline(CarveShape::Arch, Vec2d{ -0.3f, -0.3f }, Vec2d{ 0.3f, 0.3f }, {});
    const CarveOutcome outcome = CarveWithinLoopBounds(cylinder, cap, quad, Vec2d{ -0.3f, -0.3f },
                                                       Vec2d{ 0.3f, 0.3f }, *frame.Frame, outline, false, kPlanarTol);
    EXPECT_FALSE(outcome.Ok());
    EXPECT_EQ(outcome.Status(), CarveStatus::TopologyFailure);
}

namespace
{
// The springline's height on the wall, in quad UV: the coordinate along the
// axis the jambs run on. The arch's rise runs along the canonical frame's V,
// so the springline is where the arc's first point sits.
float SpringlineCoordinate(const LoopHost& host, Vec2d rectMin, Vec2d rectMax,
                           const CarveShapeParams& params, bool& outAlongU)
{
    const std::vector<Vec2d> arch = ShapeOver(host, CarveShape::Arch, rectMin, rectMax, params);
    const Bounds box = CanonicalBox(host, rectMin, rectMax);
    // The jamb top: an outline vertex on a jamb (U at the box's edge) that is
    // not a sill corner.
    for (const Vec2d& p : arch)
    {
        const bool onJamb = std::abs(p.X - box.Min.X) < 1e-4f || std::abs(p.X - box.Max.X) < 1e-4f;
        if (!onJamb || std::abs(p.Y - box.Min.Y) < 1e-4f)
            continue;
        const Vec3d rel = host.Frame.ToWorld(p) - host.Quad.Origin;
        const Vec2d uv{ rel.Dot(host.Quad.AxisU), rel.Dot(host.Quad.AxisV) };
        // Which quad axis the canonical V (the rise) runs along.
        const Vec3d rise = host.Frame.AxisV;
        outAlongU = std::abs(rise.Dot(host.Quad.AxisU)) > 0.5f;
        return outAlongU ? uv.X : uv.Y;
    }
    return 0.0f;
}

// Faces coplanar with the host, as (centroid in quad UV, vertex count).
std::vector<std::pair<Vec2d, std::size_t>> HostPlaneFaces(const LoopHost& host, const BrushMesh& mesh)
{
    const Vec3d normal = host.Quad.AxisU.Cross(host.Quad.AxisV);
    std::vector<std::pair<Vec2d, std::size_t>> faces;
    for (const BrushFace& face : mesh.Faces)
    {
        if (BrushComputeFaceNormal(mesh, face).Dot(normal) < 0.99f)
            continue;
        const Vec3d rel = BrushFaceCentroid(mesh, face) - host.Quad.Origin;
        faces.emplace_back(Vec2d{ rel.Dot(host.Quad.AxisU), rel.Dot(host.Quad.AxisV) }, face.Loop.size());
    }
    return faces;
}
}

TEST(CarveWithinLoopBounds, TheSpringlineIsARingAroundTheBrush)
{
    const LoopHost host = MakeLoopHost();
    const CarveShapeParams params;
    bool alongU = false;
    const float springline = SpringlineCoordinate(host, kInteriorMin, kInteriorMax, params, alongU);
    const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, false, params);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    const BrushMesh& mesh = outcome.Value().Mesh;

    // The cells beside the box are split at the springline into two quads
    // each, rather than carrying the springline vertex as a fifth corner.
    int besideBelow = 0;
    int besideAbove = 0;
    for (const auto& [uv, count] : HostPlaneFaces(host, mesh))
    {
        const float across = alongU ? uv.Y : uv.X; // the axis running along the springline
        const float along = alongU ? uv.X : uv.Y;  // the axis the rise runs on
        const float boxAcrossMin = alongU ? kInteriorMin.Y : kInteriorMin.X;
        const float boxAcrossMax = alongU ? kInteriorMax.Y : kInteriorMax.X;
        const float boxAlongMin = alongU ? kInteriorMin.X : kInteriorMin.Y;
        const float boxAlongMax = alongU ? kInteriorMax.X : kInteriorMax.Y;
        const bool beside = (across < boxAcrossMin || across > boxAcrossMax) && along > boxAlongMin
            && along < boxAlongMax;
        if (!beside)
            continue;
        EXPECT_EQ(count, 4u) << "a cell beside the box is not a quad: the springline is not a ring";
        (along < springline ? besideBelow : besideAbove)++;
    }
    EXPECT_EQ(besideBelow, 2) << "one cell below the springline on each side of the box";
    EXPECT_EQ(besideAbove, 2) << "one cell above the springline on each side of the box";

    // The ring goes all the way round: a vertex at the springline height exists
    // on the far wall too.
    const Vec3d normal = host.Quad.AxisU.Cross(host.Quad.AxisV);
    const Vec3d axis = alongU ? host.Quad.AxisU : host.Quad.AxisV;
    const float origin = host.Quad.Origin.Dot(axis);
    bool farVertex = false;
    for (const BrushVertex& v : mesh.Vertices)
        if ((v.Position - host.Quad.Origin).Dot(normal) < -3.0f
            && std::abs(v.Position.Dot(axis) - origin - springline) < 1e-3f)
            farVertex = true;
    EXPECT_TRUE(farVertex) << "the springline ring did not reach the far wall";
}

TEST(CarveWithinLoopBounds, AQuarterTurnedArchGetsItsRingOnTheOtherAxis)
{
    const LoopHost host = MakeLoopHost();
    CarveShapeParams params;
    params.Orientation = std::numbers::pi_v<float> * 0.5f;
    const BrushMesh control = LoopControl(host, CarveShape::Arch, kInteriorMin, kInteriorMax, params);
    const BrushMesh boxOnly = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, kInteriorMin, kInteriorMax);
    ASSERT_GT(control.Faces.size(), boxOnly.Faces.size()) << "the control's springline ring did not cut";

    for (bool pierce : { false, true })
    {
        const CarveOutcome outcome = Carve(host, CarveShape::Arch, kInteriorMin, kInteriorMax, pierce, params);
        ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
        ExpectLoopsSurvive(host, control, outcome.Value().Mesh, kInteriorMin, kInteriorMax, pierce);
        EXPECT_EQ(outcome.Value().CutFaces.size(), pierce ? 0u : 4u);
        ExpectSound(outcome.Value().Mesh);
        EXPECT_EQ(Euler(outcome.Value().Mesh), pierce ? 0 : 2);
    }
}

TEST(CarveWithinLoopBounds, EveryFaceOutsideTheCarveStaysAQuad)
{
    // A rectangle has no rim vertex but its corners: the loops alone. A diamond
    // touches the rim at four points and an arch springing from the floor at
    // its apex; each gets a ring through the point, so the faces beside the
    // box never gain a fifth vertex.
    const LoopHost host = MakeLoopHost();
    const BrushMesh boxOnly = BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face, kInteriorMin, kInteriorMax);

    CarveShapeParams quarter;
    quarter.Orientation = std::numbers::pi_v<float> * 0.5f;
    for (const CarveShapeParams& params : { CarveShapeParams{}, quarter })
    {
        const CarveOutcome outcome = Carve(host, CarveShape::Rectangle, kInteriorMin, kInteriorMax, false, params);
        ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
        EXPECT_EQ(outcome.Value().Mesh.Faces.size(), boxOnly.Faces.size());
        EXPECT_EQ(outcome.Value().Mesh.Vertices.size(), boxOnly.Vertices.size());
    }

    CarveShapeParams eighth;
    eighth.Orientation = std::numbers::pi_v<float> * 0.25f;
    CarveShapeParams fromFloor;
    fromFloor.ArchRise = 1.0f;
    const std::pair<CarveShape, CarveShapeParams> shapes[] = { { CarveShape::Rectangle, eighth },
                                                               { CarveShape::Arch, fromFloor } };
    for (const auto& [shape, params] : shapes)
        for (bool pierce : { false, true })
        {
            const BrushMesh control = LoopControl(host, shape, kInteriorMin, kInteriorMax, params);
            ASSERT_GT(control.Faces.size(), boxOnly.Faces.size()) << "the control's rings did not cut";
            const CarveOutcome outcome = Carve(host, shape, kInteriorMin, kInteriorMax, pierce, params);
            ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
            ExpectLoopsSurvive(host, control, outcome.Value().Mesh, kInteriorMin, kInteriorMax, pierce);
            ExpectSound(outcome.Value().Mesh);
            const Vec3d normal = host.Quad.AxisU.Cross(host.Quad.AxisV);
            for (const BrushFace& face : outcome.Value().Mesh.Faces)
            {
                if (BrushComputeFaceNormal(outcome.Value().Mesh, face).Dot(normal) < 0.99f)
                    continue;
                const Vec3d rel = BrushFaceCentroid(outcome.Value().Mesh, face) - host.Quad.Origin;
                const Vec2d uv{ rel.Dot(host.Quad.AxisU), rel.Dot(host.Quad.AxisV) };
                const bool inBox = uv.X > kInteriorMin.X && uv.X < kInteriorMax.X && uv.Y > kInteriorMin.Y
                    && uv.Y < kInteriorMax.Y;
                if (!inBox)
                {
                    EXPECT_EQ(face.Loop.size(), 4u) << "a face outside the carve is not a quad";
                }
            }
        }
}
