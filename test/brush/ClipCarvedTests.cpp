// A plane through a solid the carve has already worked on: concave faces,
// several section contours, nested ones, and cuts that dismember.
#include "brush/BrushFaceFrame.h"
#include "brush/BrushHalfEdge.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <set>
#include <string>

namespace
{
constexpr float kTol = 1e-3f;

std::uint32_t FaceFacing(const BrushMesh& mesh, Vec3d d)
{
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(mesh, mesh.Faces[i]).Dot(d) > 0.99f)
            return i;
    return 0xFFFFFFFFu;
}

int Euler(const BrushMesh& mesh)
{
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i], b = face.Loop[(i + 1) % face.Loop.size()];
            edges.insert({ std::min(a, b), std::max(a, b) });
        }
    return int(mesh.Vertices.size()) - int(edges.size()) + int(mesh.Faces.size());
}

BrushRepairResult Validate(const BrushMesh& mesh)
{
    BrushMesh copy = mesh;
    return BrushValidateAndRepair(copy);
}

// Six times the signed volume, by the divergence sum over each face's fan:
// positive for an outward-wound closed solid.
double SignedVolume6(const BrushMesh& mesh)
{
    double total = 0.0;
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 1; i + 1 < face.Loop.size(); ++i)
        {
            const Vec3d a = mesh.Vertices[face.Loop[0]].Position;
            const Vec3d b = mesh.Vertices[face.Loop[i]].Position;
            const Vec3d c = mesh.Vertices[face.Loop[i + 1]].Position;
            total += static_cast<double>(a.Dot(b.Cross(c)));
        }
    return total;
}

// What a clip must hand back: a usable mesh the repair had nothing to do to,
// closed, every edge used exactly twice in opposite directions, every face a
// real polygon, and positive volume. `label` names the case in a failure.
void ExpectSolid(const BrushMesh& mesh, const std::string& label)
{
    BrushMesh copy = mesh;
    const BrushRepairResult report = BrushValidateAndRepair(copy);
    EXPECT_TRUE(report.Ok) << label;
    EXPECT_TRUE(report.Closed) << label << ": open";
    EXPECT_FALSE(report.Changed) << label << ": the repair had to change it";
    for (const BrushFace& face : mesh.Faces)
    {
        EXPECT_GE(face.Loop.size(), 3u) << label;
        Vec3d sum{};
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
            sum = sum + mesh.Vertices[face.Loop[i]].Position.Cross(mesh.Vertices[face.Loop[(i + 1) % face.Loop.size()]].Position);
        // A sliver a cut leaves at a corner is small, not zero: the repair's
        // own test is a zero normal, and that is what must not survive.
        EXPECT_GT(sum.Magnitude() * 0.5f, 0.0f) << label << ": zero-area face";
    }
    const BrushHalfEdgeMesh half = BrushBuildHalfEdge(mesh);
    for (std::size_t h = 0; h < half.HalfEdges.size(); ++h)
    {
        const BrushHalfEdge& edge = half.HalfEdges[h];
        ASSERT_NE(edge.Twin, BrushInvalidIndex) << label << ": a half-edge without a twin";
        const BrushHalfEdge& twin = half.HalfEdges[edge.Twin];
        EXPECT_EQ(twin.Twin, h) << label;
        EXPECT_EQ(twin.Origin, half.HalfEdges[edge.Next].Origin) << label << ": twin runs the same way";
    }
    EXPECT_GT(SignedVolume6(mesh), 1e-6) << label << ": no volume, or wound inward";
}

// Total area of the faces whose normal is `normal`, and of those lying in `plane`.
float AreaFacing(const BrushMesh& mesh, Vec3d normal)
{
    float total = 0.0f;
    for (const BrushFace& face : mesh.Faces)
    {
        if (BrushComputeFaceNormal(mesh, face).Dot(normal) < 0.99f)
            continue;
        Vec3d sum{};
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
            sum = sum + mesh.Vertices[face.Loop[i]].Position.Cross(mesh.Vertices[face.Loop[(i + 1) % face.Loop.size()]].Position);
        total += std::abs(sum.Dot(normal)) * 0.5f;
    }
    return total;
}

// A box with an arch carved into its +X wall: on the floor edge (a doorway,
// a notch when pierced) or raised (a window, a tunnel when pierced).
BrushMesh CarvedBox(bool pierce, float lift)
{
    BrushMesh box = BrushOps::MakeBox({ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, { 1, 0, 0 });
    const BrushFaceFrame frame = *FaceFrame(box, face, kTol).Frame;
    Vec2d lo = frame.Outline.front(), hi = lo;
    for (const Vec2d& p : frame.Outline)
    {
        lo = { std::min(lo.X, p.X), std::min(lo.Y, p.Y) };
        hi = { std::max(hi.X, p.X), std::max(hi.Y, p.Y) };
    }
    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, { lo.X + 1.0f, lo.Y + lift }, { hi.X - 1.0f, lo.Y + lift + 3.0f }, { 0.5f, 8 });
    const CarveOutcome out = pierce ? CarveFacePolygonThrough(box, face, frame, arch, kTol)
                                    : CarveFacePolygon(box, face, frame, arch);
    EXPECT_TRUE(out.Ok()) << CarveStatusText(out.Status());
    return out.Ok() ? out.Value().Mesh : box;
}

void ExpectClosedHalves(const BrushMesh& solid, const Plane& plane, int euler, std::size_t components = 1)
{
    for (const bool keep : { true, false })
    {
        const BrushMesh half = BrushOps::Clip(solid, plane, keep);
        ASSERT_FALSE(half.Faces.empty()) << "the cut was refused";
        const BrushRepairResult report = Validate(half);
        EXPECT_TRUE(report.Ok);
        EXPECT_TRUE(report.Closed) << (keep ? "front" : "back") << " half is open";
        EXPECT_EQ(BrushConnectedComponents(half).size(), components);
        if (components == 1)
        {
            EXPECT_EQ(Euler(half), euler) << (keep ? "front" : "back");
        }
    }
}
}

TEST(ClipCarved, AHorizontalCutThroughTheJambsOfAFlatArch)
{
    // The surround around the arch is concave and the plane crosses it four
    // times. A flat carve changes no volume, so each half is a box with the
    // arch's faces above or below the cut and a rectangular cap.
    const BrushMesh solid = CarvedBox(false, 0.0f);
    const Plane plane = Plane::FromNormalAndPoint({ 0, 1, 0 }, { 0, 0, 0 });
    ExpectClosedHalves(solid, plane, 2);
    const BrushMesh below = BrushOps::Clip(solid, plane, false);
    // Wall faces of the lower half cover exactly the lower half of the wall:
    // nothing was filled in across the opening along the cut.
    EXPECT_NEAR(AreaFacing(below, { 1, 0, 0 }), 4.0f * 2.0f, 1e-3f);
    EXPECT_NEAR(AreaFacing(below, { 0, 1, 0 }), 16.0f, 1e-3f) << "one rectangular cap";
}

TEST(ClipCarved, ACutAcrossATunnelHasAnAnnularCap)
{
    const BrushMesh tunnel = CarvedBox(true, 0.5f);
    ASSERT_EQ(Euler(tunnel), 0) << "the fixture is a tunnel";
    const Plane plane = Plane::FromNormalAndPoint({ 1, 0, 0 }, { 0, 0, 0 });
    ExpectClosedHalves(tunnel, plane, 0); // each half still has the hole through it
    const BrushMesh half = BrushOps::Clip(tunnel, plane, true);
    // The cap is the wall's area minus the arch's, in however many pieces the
    // bridging needed, none of which crosses the hole.
    const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, { -1.0f, -1.5f }, { 1.0f, 1.5f }, { 0.5f, 8 });
    float archArea = 0.0f;
    for (std::size_t i = 0; i < arch.size(); ++i)
        archArea += arch[i].X * arch[(i + 1) % arch.size()].Y - arch[(i + 1) % arch.size()].X * arch[i].Y;
    archArea = std::abs(archArea) * 0.5f;
    EXPECT_NEAR(AreaFacing(half, { -1, 0, 0 }), 16.0f - archArea, 1e-2f);
}

TEST(ClipCarved, ACutAlongATunnelHasTwoCaps)
{
    const BrushMesh tunnel = CarvedBox(true, 0.5f);
    const Plane plane = Plane::FromNormalAndPoint({ 0, 0, 1 }, { 0, 0, 0 });
    ExpectClosedHalves(tunnel, plane, 2); // each half is a grooved block: genus 0
    const BrushMesh half = BrushOps::Clip(tunnel, plane, true);
    int caps = 0;
    for (const BrushFace& face : half.Faces)
    {
        bool inPlane = true;
        for (std::uint32_t v : face.Loop)
            inPlane = inPlane && std::abs(half.Vertices[v].Position.Z) < 1e-3f;
        caps += inPlane ? 1 : 0;
    }
    EXPECT_EQ(caps, 2) << "one cap above the tunnel and one below";
}

TEST(ClipCarved, ACutThroughADoorwayDismembersIntoTwoShells)
{
    const BrushMesh doorway = CarvedBox(true, 0.0f);
    const Plane plane = Plane::FromNormalAndPoint({ 0, 1, 0 }, { 0, 0, 0 });
    const BrushMesh below = BrushOps::Clip(doorway, plane, false);
    ASSERT_FALSE(below.Faces.empty());
    EXPECT_TRUE(Validate(below).Closed);
    const std::vector<BrushMesh> shells = BrushConnectedComponents(below);
    ASSERT_EQ(shells.size(), 2u) << "the two jambs";
    for (const BrushMesh& shell : shells)
    {
        EXPECT_TRUE(Validate(shell).Closed);
        EXPECT_EQ(Euler(shell), 2);
    }
    const BrushMesh above = BrushOps::Clip(doorway, plane, true);
    EXPECT_EQ(BrushConnectedComponents(above).size(), 1u);
    EXPECT_TRUE(Validate(above).Closed);
}

TEST(ClipCarved, AGridAlignedCutThroughExistingVertices)
{
    // A box loop-cut at y = 0, then clipped exactly there: every crossing is an
    // existing vertex. One cap, nothing doubled, both halves closed.
    BrushMesh box = BrushOps::MakeBox({ 2, 2, 2 });
    const std::uint32_t wall = FaceFacing(box, { 1, 0, 0 });
    const BrushOps::BrushRectFaceFrame quad = *BrushOps::RectFaceFrame(box, wall);
    const BrushMesh cut = BrushOps::InsertFaceLoopBounds(box, wall, { 0, 0 }, { quad.Width * 0.5f, quad.Height });
    ASSERT_GT(cut.Faces.size(), box.Faces.size());
    const Plane plane = Plane::FromNormalAndPoint({ 0, 1, 0 }, { 0, 0, 0 });
    ExpectClosedHalves(cut, plane, 2);
    const BrushMesh half = BrushOps::Clip(cut, plane, true);
    EXPECT_EQ(half.Vertices.size(), 8u) << "the ring's vertices are the cap's, not duplicated";
    EXPECT_EQ(half.Faces.size(), 6u);
}

TEST(ClipCarved, APlaneOnAFaceKeepsTheSolidWholeOrNothing)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const Plane onFace = Plane::FromNormalAndPoint({ 1, 0, 0 }, { 1, 0, 0 });
    const BrushMesh whole = BrushOps::Clip(box, onFace, false);
    EXPECT_EQ(whole.Faces.size(), 6u) << "no cap on a face that is the plane";
    EXPECT_TRUE(Validate(whole).Closed);
    const BrushMesh nothing = BrushOps::Clip(box, onFace, true);
    EXPECT_TRUE(nothing.Faces.empty());
}

TEST(ClipCarved, ACutAlongTheSpringlineOfAFlatArch)
{
    // The plane holds the springline vertices of the arch and its surround:
    // on-plane runs on concave faces, and still one closed cut.
    const BrushMesh solid = CarvedBox(false, 0.0f);
    const Plane plane = Plane::FromNormalAndPoint({ 0, 1, 0 }, { 0, -2.0f + 1.5f, 0 }); // springline: rise 0.5 of 3
    ExpectClosedHalves(solid, plane, 2);
}

namespace
{
enum class CarveMode { Polygon, Quad };

// A wall (half extents 2.5 x 2.5 x 0.5) with an arch doorway pierced through
// its +Z face by the tool's own paths: Polygon mode lays the outline across the
// face's surface, Quad mode wraps loop cuts through the box bounds and every
// rim vertex first. The arch spans x in [-1, 1], springs at y = -1, crowns at
// y = 0.5 with the apex at x = 0.
BrushMesh Doorway(CarveMode mode, int segments)
{
    BrushMesh box = BrushOps::MakeBox({ 2.5f, 2.5f, 0.5f });
    const std::uint32_t face = FaceFacing(box, { 0, 0, 1 });
    const BrushFaceFrame frame = *FaceFrame(box, face, kTol).Frame;
    Vec2d lo = frame.Outline.front(), hi = lo;
    for (const Vec2d& p : frame.Outline)
    {
        lo = { std::min(lo.X, p.X), std::min(lo.Y, p.Y) };
        hi = { std::max(hi.X, p.X), std::max(hi.Y, p.Y) };
    }
    const Vec2d boxMin{ lo.X + 1.5f, lo.Y };
    const Vec2d boxMax{ hi.X - 1.5f, lo.Y + 3.0f };
    const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, boxMin, boxMax, { 0.5f, segments });
    CarveOutcome out = CarveOutcome::Failure(CarveStatus::TopologyFailure);
    if (mode == CarveMode::Quad)
    {
        const BrushOps::BrushRectFaceFrame quad = *BrushOps::RectFaceFrame(box, face);
        const auto toQuad = [&](Vec2d p) {
            const Vec3d r = frame.Origin + frame.AxisU * p.X + frame.AxisV * p.Y - quad.Origin;
            return Vec2d{ r.Dot(quad.AxisU), r.Dot(quad.AxisV) };
        };
        const Vec2d a = toQuad(boxMin), b = toQuad(boxMax);
        out = CarveWithinLoopBounds(box, face, quad, { std::min(a.X, b.X), std::min(a.Y, b.Y) },
                                    { std::max(a.X, b.X), std::max(a.Y, b.Y) }, frame, arch, /*pierce*/ true, kTol);
    }
    else
    {
        const std::vector<FaceCorners> surface{ CornersOf(box, face) };
        out = CarveAcrossSurface(box, surface, frame, arch, /*pierce*/ true, kTol);
    }
    EXPECT_TRUE(out.Ok()) << CarveStatusText(out.Status());
    return out.Ok() ? out.Value().Mesh : box;
}

// The x of the first arch vertex clockwise from the crown, for a cut through
// an arch-segment vertex.
float FirstArchVertexX(int segments)
{
    const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, { -1.0f, -2.5f }, { 1.0f, 0.5f }, { 0.5f, segments });
    float best = 0.0f;
    for (const Vec2d& p : arch)
        if (p.Y > -1.0f + 1e-3f && p.X > 1e-3f && (best == 0.0f || p.X < best))
            best = p.X;
    return best;
}

struct DoorwayCut
{
    const char* Name;
    Plane Cut;
    std::size_t FrontComponents;
    std::size_t BackComponents;
};

void ExpectDoorwayCut(const BrushMesh& solid, const DoorwayCut& cut, const std::string& fixture)
{
    for (const bool keep : { true, false })
    {
        const std::string label = fixture + " " + cut.Name + (keep ? " front" : " back");
        const BrushMesh half = BrushOps::Clip(solid, cut.Cut, keep);
        ASSERT_FALSE(half.Faces.empty()) << label << ": the cut was refused";
        ExpectSolid(half, label);
        const std::vector<BrushMesh> shells = BrushConnectedComponents(half);
        EXPECT_EQ(shells.size(), keep ? cut.FrontComponents : cut.BackComponents) << label;
        for (const BrushMesh& shell : shells)
            ExpectSolid(shell, label + " shell");
    }
}
}

TEST(ClipCarved, EveryCutThroughAPiercedDoorwayCloses)
{
    // The cuts a snapped gesture near a doorway makes, on both carve modes:
    // through the opening, on and beside the crown (Quad mode carries a ring
    // there), on and beside a jamb (the plane then contains the jamb face),
    // through an arch vertex, a slight diagonal, and the ring planes.
    const auto vertical = [](float x) { return Plane::FromNormalAndPoint({ 1, 0, 0 }, { x, 0, 0 }); };
    const auto horizontal = [](float y) { return Plane::FromNormalAndPoint({ 0, 1, 0 }, { 0, y, 0 }); };
    const float tilt = 3.0f * std::numbers::pi_v<float> / 180.0f;
    for (const CarveMode mode : { CarveMode::Polygon, CarveMode::Quad })
        for (const int segments : { 4, 8 })
        {
            const BrushMesh solid = Doorway(mode, segments);
            ExpectSolid(solid, "fixture");
            const float archX = FirstArchVertexX(segments);
            const DoorwayCut cuts[] = {
                { "through the opening off-centre", vertical(-0.3f), 1, 1 },
                { "through the crown", vertical(0.0f), 1, 1 },
                { "1e-4 beside the crown", vertical(1e-4f), 1, 1 },
                { "-1e-4 beside the crown", vertical(-1e-4f), 1, 1 },
                { "2.5e-4 beside the crown", vertical(2.5e-4f), 1, 1 },
                { "-2.5e-4 beside the crown", vertical(-2.5e-4f), 1, 1 },
                { "1e-3 beside the crown", vertical(1e-3f), 1, 1 },
                { "through an arch vertex", vertical(archX), 1, 1 },
                { "1e-4 beside an arch vertex", vertical(archX + 1e-4f), 1, 1 },
                { "on the left jamb", vertical(-1.0f), 1, 1 },
                { "1e-4 beside the left jamb", vertical(-1.0f + 1e-4f), 1, 1 },
                { "on the right jamb", vertical(1.0f), 1, 1 },
                { "1e-4 beside the right jamb", vertical(1.0f - 1e-4f), 1, 1 },
                { "slight diagonal", Plane::FromNormalAndPoint({ std::cos(tilt), std::sin(tilt), 0 }, { -0.3f, 0, 0 }), 1, 1 },
                { "at the springline", horizontal(-1.0f), 1, 2 },
                { "at crown height", horizontal(0.5f), 1, 1 },
                { "below the springline", horizontal(-1.5f), 1, 2 },
            };
            const std::string fixture = std::string(mode == CarveMode::Quad ? "quad" : "polygon") + "/" + std::to_string(segments);
            for (const DoorwayCut& cut : cuts)
                ExpectDoorwayCut(solid, cut, fixture);
        }
}

TEST(ClipCarved, APlaneOnTheInnerFaceOfAnLBlockCapsAcrossTheReflexEdge)
{
    // An L: a box with its upper-right quarter carved off (a flat rectangle
    // carve, pierced, on the +Z face leaves a notch through the whole block).
    // The plane on the notch's vertical face meets the notch's floor at a
    // reflex edge: the plane continues into the solid below the floor, so the
    // cap there must reach that edge; the half on the other side has the
    // face itself and no cap.
    BrushMesh box = BrushOps::MakeBox({ 2, 2, 1 });
    const std::uint32_t face = FaceFacing(box, { 0, 0, 1 });
    const BrushFaceFrame frame = *FaceFrame(box, face, kTol).Frame;
    Vec2d lo = frame.Outline.front(), hi = lo;
    for (const Vec2d& p : frame.Outline)
    {
        lo = { std::min(lo.X, p.X), std::min(lo.Y, p.Y) };
        hi = { std::max(hi.X, p.X), std::max(hi.Y, p.Y) };
    }
    const std::vector<Vec2d> notch = CarveShapeOutline(CarveShape::Rectangle, { lo.X + 2.0f, lo.Y + 2.0f }, hi, {});
    const std::vector<FaceCorners> surface{ CornersOf(box, face) };
    const CarveOutcome out = CarveAcrossSurface(box, surface, frame, notch, /*pierce*/ true, kTol);
    ASSERT_TRUE(out.Ok()) << CarveStatusText(out.Status());
    const BrushMesh block = out.Value().Mesh;
    ExpectSolid(block, "L block");

    // The notch's vertical face is at x = 0 (or -0: the frame may mirror U).
    const Plane plane = Plane::FromNormalAndPoint({ 1, 0, 0 }, { 0, 0, 0 });
    for (const bool keep : { true, false })
    {
        const BrushMesh half = BrushOps::Clip(block, plane, keep);
        ASSERT_FALSE(half.Faces.empty()) << (keep ? "front" : "back");
        ExpectSolid(half, keep ? "front" : "back");
        EXPECT_EQ(BrushConnectedComponents(half).size(), 1u);
        int inPlane = 0;
        for (const BrushFace& f : half.Faces)
        {
            bool flat = true;
            for (std::uint32_t v : f.Loop)
                flat = flat && std::abs(half.Vertices[v].Position.X) < 1e-3f;
            inPlane += flat ? 1 : 0;
        }
        // Front (x > 0) keeps the lower-right block: its face in the plane is
        // the one cap, 2 x 2. Back (x < 0) keeps the left slab: the notch wall
        // as it stands plus the cap under it, tiling the slab's whole side
        // without overlap, 4 x 2 in two faces.
        EXPECT_NEAR(AreaFacing(half, keep ? Vec3d{ -1, 0, 0 } : Vec3d{ 1, 0, 0 }), keep ? 4.0f : 8.0f, 1e-3f)
            << (keep ? "front" : "back");
        EXPECT_EQ(inPlane, keep ? 1 : 2) << (keep ? "front" : "back");
    }
}

TEST(ClipCarved, APlaneOnABoxFaceKeepsTheSolidSideWholeAndTheOtherEmpty)
{
    // Keep the half-space containing the solid: the box, untouched, no cap.
    // Keep the other: nothing has volume there, so nothing; the tool never
    // asks, since no vertex lies strictly on that side.
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const Plane onFace = Plane::FromNormalAndPoint({ 1, 0, 0 }, { 1, 0, 0 });
    const BrushMesh whole = BrushOps::Clip(box, onFace, false);
    ExpectSolid(whole, "solid side");
    EXPECT_EQ(whole.Faces.size(), 6u);
    EXPECT_EQ(whole.Vertices.size(), 8u);
    EXPECT_TRUE(BrushOps::Clip(box, onFace, true).Faces.empty());
    bool strictlyBeyond = false;
    for (const BrushVertex& v : box.Vertices)
        strictlyBeyond = strictlyBeyond || onFace.SignedDistanceTo(v.Position) > BrushOps::kClipSnap;
    EXPECT_FALSE(strictlyBeyond) << "the tool reports this as a miss";
}
