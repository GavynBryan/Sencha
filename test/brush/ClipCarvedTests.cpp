// A plane through a solid the carve has already worked on: concave faces,
// several section contours, nested ones, and cuts that dismember.
#include "brush/BrushFaceFrame.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

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
