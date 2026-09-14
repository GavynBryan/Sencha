#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

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

// Every face still outward, the solid still closed, nothing for repair to fix.
void ExpectSound(const BrushMesh& mesh)
{
    BrushMesh copy = mesh;
    const BrushRepairResult report = BrushValidateAndRepair(copy);
    EXPECT_TRUE(report.Ok);
    EXPECT_TRUE(report.Closed) << "the carve opened the solid";
    EXPECT_FALSE(report.Changed) << "repair had to clean up after the carve";

    const Vec3d center = BrushMeshCentroid(mesh);
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
    {
        const Vec3d normal = BrushComputeFaceNormal(mesh, mesh.Faces[i]);
        EXPECT_GT(normal.Dot(BrushFaceCentroid(mesh, mesh.Faces[i]) - center), 0.0f)
            << "face " << i << " points inward";
    }
}
}

TEST(CarveFacePolygon, ADoorwayFlushWithTheFloorLeavesOneSurroundFace)
{
    // One face around the opening, with no edges radiating from its corners.
    // That is the whole point of the surround walk, and it is what the old
    // trapezoid ring could not produce.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    ASSERT_NE(face, 0xFFFFFFFFu);
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y, bounds.Min.X + 2.5f, bounds.Min.Y + 2.5f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 1u);
    ASSERT_EQ(outcome.Value().CutFaces.size(), 1u);
    EXPECT_EQ(outcome.Value().CutFaces.front(), face);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveFacePolygon, AWindowStrictlyInsideLeavesTwoSurroundFaces)
{
    // A hole touching nothing costs two bridges, so two faces. One bridge would
    // leave a single face with a doubled edge, which the ear clipper silently
    // fans into a bowtie that survives repair.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 2u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveFacePolygon, ASecondCarveSitsFlushAgainstTheFirst)
{
    // Complaint one. The first flush carve turns the host into a hexagon, and
    // the old kernel's quad gate made the second carve impossible by
    // construction, so a designer could never place two openings side by side.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome first = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 0.5f, bounds.Min.Y, bounds.Min.X + 1.5f, bounds.Min.Y + 2.0f));
    ASSERT_TRUE(first.Ok()) << CarveStatusText(first.Status());
    ASSERT_EQ(first.Value().SurroundFaces.size(), 1u);

    const BrushMesh& carved = first.Value().Mesh;
    const std::uint32_t host = first.Value().SurroundFaces.front();
    ASSERT_GT(carved.Faces[host].Loop.size(), 4u) << "the leftover host should be an n-gon";
    EXPECT_FALSE(BrushOps::RectFaceFrame(carved, host).has_value())
        << "the old rectangle gate accepted this host after all";

    const BrushFaceFrameResult second = FaceFrame(carved, host, kPlanarTol);
    ASSERT_TRUE(second.Frame.has_value()) << CarveStatusText(second.Status);

    // Sharing the first opening's right jamb exactly, and the floor with it.
    const CarveOutcome outcome = CarveFacePolygon(
        carved, host, *second.Frame,
        Rect(bounds.Min.X + 1.5f, bounds.Min.Y, bounds.Min.X + 2.5f, bounds.Min.Y + 2.0f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 1u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveFacePolygon, AnArchCarvesLikeAnyOtherOutline)
{
    // The shape is a generator, not a second topology path.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const std::vector<Vec2d> arch = CarveShapeOutline(
        CarveShape::Arch, Vec2d{ bounds.Min.X + 1.0f, bounds.Min.Y },
        Vec2d{ bounds.Min.X + 2.5f, bounds.Min.Y + 2.5f }, { 0.6f, 8 });

    const CarveOutcome outcome = CarveFacePolygon(box, face, *frame.Frame, arch);
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 1u);
    EXPECT_EQ(outcome.Value().Mesh.Faces[outcome.Value().CutFaces.front()].Loop.size(), arch.size());
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveFacePolygon, EveryPieceInheritsTheHostMaterial)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    box.Faces[face].Material.Uv.Rotation = 22.0f;
    box.Faces[face].Material.Uv.Scale = Vec2d{ 0.5f, 1.5f };
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());

    const BrushMesh& carved = outcome.Value().Mesh;
    std::vector<std::uint32_t> produced = outcome.Value().SurroundFaces;
    produced.push_back(outcome.Value().CutFaces.front());
    for (std::uint32_t index : produced)
    {
        EXPECT_FLOAT_EQ(carved.Faces[index].Material.Uv.Rotation, 22.0f);
        EXPECT_FLOAT_EQ(carved.Faces[index].Material.Uv.Scale.X, 0.5f);
        EXPECT_FLOAT_EQ(carved.Faces[index].Material.Uv.Scale.Y, 1.5f);
    }
}

TEST(CarveFacePolygon, TheNeighbourGainsTheFlushCornersRatherThanATJunction)
{
    // A flush carve splits the floor's edge. If the bordering face does not
    // gain the same vertices it keeps a longer edge across two shorter ones,
    // and the seam cracks open the first time either side moves.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const std::uint32_t floor = FaceFacing(box, Vec3d{ 0, -1, 0 });
    ASSERT_NE(floor, 0xFFFFFFFFu);
    const std::size_t before = box.Faces[floor].Loop.size();
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    // The frame's V axis runs along -Y here, so the rim it is flush with is the
    // one the floor borders; whichever face that is, one of them gains two.
    const CarveOutcome outcome = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y, bounds.Min.X + 2.5f, bounds.Min.Y + 2.0f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());

    const BrushMesh& carved = outcome.Value().Mesh;
    std::size_t grown = 0;
    for (std::uint32_t i = 0; i < carved.Faces.size(); ++i)
        if (i != face && i < box.Faces.size() && carved.Faces[i].Loop.size() > box.Faces[i].Loop.size())
            ++grown;
    EXPECT_EQ(grown, 1u) << "exactly the bordering face should have gained the flush corners";
    EXPECT_GE(before, 4u);
    ExpectSound(carved);
}

TEST(CarveFacePolygon, ARefusalCarriesNoMeshAndSaysWhy)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outside = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Max.X + 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X + 3.0f, bounds.Min.Y + 2.0f));
    EXPECT_FALSE(outside.Ok());
    EXPECT_EQ(outside.Status(), CarveStatus::OutsideHost);

    const CarveOutcome hanging = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Max.X - 1.0f, bounds.Min.Y + 1.0f, bounds.Max.X + 1.0f, bounds.Min.Y + 2.0f));
    EXPECT_FALSE(hanging.Ok()) << "a shape hanging off the edge is not a carve";
    EXPECT_EQ(hanging.Status(), CarveStatus::OutsideHost);

    const std::vector<Vec2d> sliver = { Vec2d{ 0, 0 }, Vec2d{ 1e-6f, 0 }, Vec2d{ 0, 1e-6f } };
    EXPECT_EQ(CarveFacePolygon(box, face, *frame.Frame, sliver).Status(), CarveStatus::DegenerateEdge);

    const std::vector<Vec2d> clockwise = Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 1.0f,
                                              bounds.Max.X - 1.0f, bounds.Max.Y - 1.0f);
    std::vector<Vec2d> reversed(clockwise.rbegin(), clockwise.rend());
    EXPECT_EQ(CarveFacePolygon(box, face, *frame.Frame, reversed).Status(), CarveStatus::InvalidOutline);

    const CarveOutcome covering = CarveFacePolygon(
        box, face, *frame.Frame, Rect(bounds.Min.X, bounds.Min.Y, bounds.Max.X, bounds.Max.Y));
    EXPECT_FALSE(covering.Ok()) << "carving the whole face away is not a carve";
}

TEST(CarveFacePolygon, AShapeJustOffTheRimSnapsFlushRatherThanLeavingASliver)
{
    // Projecting onto a rim edge can leave a point a fraction outside the weld
    // tolerance, and that fraction becomes an edge nothing later removes.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y + 5e-5f, bounds.Min.X + 2.5f, bounds.Min.Y + 2.0f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 1u) << "it should have landed flush";

    BrushMesh copy = outcome.Value().Mesh;
    const std::size_t before = copy.Vertices.size();
    BrushValidateAndRepair(copy);
    EXPECT_EQ(copy.Vertices.size(), before) << "a weld had something to remove";
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveFacePolygon, AShapeSpanningOppositeRimsSplitsTheFaceInTwo)
{
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 2, 2, 2 });
    const std::uint32_t face = FaceFacing(box, Vec3d{ 1, 0, 0 });
    const BrushFaceFrameResult frame = FaceFrame(box, face, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygon(
        box, face, *frame.Frame,
        Rect(bounds.Min.X + 1.0f, bounds.Min.Y, bounds.Min.X + 2.5f, bounds.Max.Y));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());
    EXPECT_EQ(outcome.Value().SurroundFaces.size(), 2u);
    ExpectSound(outcome.Value().Mesh);
}

TEST(CarveFacePolygon, OnAPlaneEveryPieceStillFacesTheSameWay)
{
    // An open mesh has no inside for the outward test to lean on, so the winding
    // is checked against the face it came from instead.
    const BrushMesh plane = BrushOps::MakePlane(Vec3d{ 2, 0, 2 }, /*depthAxis*/ 1);
    ASSERT_EQ(plane.Faces.size(), 1u);
    const Vec3d before = BrushComputeFaceNormal(plane, plane.Faces[0]);
    const BrushFaceFrameResult frame = FaceFrame(plane, 0, kPlanarTol);
    ASSERT_TRUE(frame.Frame.has_value());
    const Bounds bounds = OutlineBounds(*frame.Frame);

    const CarveOutcome outcome = CarveFacePolygon(
        plane, 0, *frame.Frame,
        Rect(bounds.Min.X + 0.5f, bounds.Min.Y + 0.5f, bounds.Max.X - 0.5f, bounds.Max.Y - 0.5f));
    ASSERT_TRUE(outcome.Ok()) << CarveStatusText(outcome.Status());

    const BrushMesh& carved = outcome.Value().Mesh;
    for (std::uint32_t i = 0; i < carved.Faces.size(); ++i)
        EXPECT_GT(BrushComputeFaceNormal(carved, carved.Faces[i]).Dot(before), 0.99f)
            << "face " << i << " flipped";
}

