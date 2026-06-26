#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace
{
    // Every undirected edge shared by exactly two faces == closed solid.
    bool IsClosed(const BrushMesh& mesh)
    {
        BrushMesh copy = mesh;
        return BrushValidateAndRepair(copy).Closed;
    }

    bool AllNormalsOutward(const BrushMesh& mesh)
    {
        const Vec3d center = BrushMeshCentroid(mesh);
        for (const BrushFace& face : mesh.Faces)
        {
            const Vec3d normal = BrushComputeFaceNormal(mesh, face);
            if (normal.Dot(BrushFaceCentroid(mesh, face) - center) <= 0.0f)
                return false;
        }
        return true;
    }
}

TEST(BrushOps, MakeBoxIsClosedOutwardSixQuads)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    EXPECT_EQ(box.Vertices.size(), 8u);
    ASSERT_EQ(box.Faces.size(), 6u);
    for (const BrushFace& face : box.Faces)
        EXPECT_EQ(face.Loop.size(), 4u); // quads, not tris
    EXPECT_TRUE(IsClosed(box));
    EXPECT_TRUE(AllNormalsOutward(box));

    const Aabb3d bounds = BrushComputeBounds(box);
    EXPECT_FLOAT_EQ(bounds.Min.X, -1.0f);
    EXPECT_FLOAT_EQ(bounds.Max.X, 1.0f);
}

TEST(BrushOps, TranslateMovesAllVertices)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const BrushMesh moved = BrushOps::Translate(box, { 5.0f, 0.0f, 0.0f });
    const Aabb3d bounds = BrushComputeBounds(moved);
    EXPECT_FLOAT_EQ(bounds.Min.X, 4.0f);
    EXPECT_FLOAT_EQ(bounds.Max.X, 6.0f);
    EXPECT_TRUE(IsClosed(moved));
}

TEST(BrushOps, ExtrudeFacePreservesClosednessAndExtendsBounds)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    // Find the +X face.
    std::uint32_t plusX = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.9f)
            plusX = i;

    const BrushMesh extruded = BrushOps::ExtrudeFace(box, plusX, 2.0f);
    EXPECT_TRUE(IsClosed(extruded));
    EXPECT_EQ(extruded.Vertices.size(), 12u);          // 8 + 4 extruded
    EXPECT_EQ(extruded.Faces.size(), 10u);             // 6 + 4 side walls
    EXPECT_TRUE(AllNormalsOutward(extruded));
    EXPECT_FLOAT_EQ(BrushComputeBounds(extruded).Max.X, 3.0f);
}

TEST(BrushOps, ResizeFaceMovesOneFace)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::uint32_t plusX = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.9f)
            plusX = i;

    const BrushMesh resized = BrushOps::ResizeFace(box, plusX, 3.0f, 0.1f);
    EXPECT_TRUE(IsClosed(resized));
    EXPECT_FLOAT_EQ(BrushComputeBounds(resized).Max.X, 3.0f);
    EXPECT_FLOAT_EQ(BrushComputeBounds(resized).Min.X, -1.0f); // opposite face unchanged
}

TEST(BrushOps, ResizeFaceClampsToMinThickness)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::uint32_t plusX = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.9f)
            plusX = i;

    // Ask to push the +X face far past the -X face; clamp keeps min thickness.
    const BrushMesh resized = BrushOps::ResizeFace(box, plusX, -5.0f, 0.25f);
    EXPECT_GE(BrushComputeBounds(resized).Max.X, -1.0f + 0.25f - 1e-3f);
    EXPECT_TRUE(IsClosed(resized));
}

TEST(BrushOps, DeleteFaceOpensTheMesh)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const BrushMesh opened = BrushOps::DeleteFace(box, 0);
    EXPECT_EQ(opened.Faces.size(), 5u);
    EXPECT_FALSE(IsClosed(opened)); // open mesh — allowed during authoring
}

TEST(BrushOps, FlipFaceReversesWindingAndSurvivesRepair)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::uint32_t plusX = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.9f)
            plusX = i;

    const Vec3d original = BrushComputeFaceNormal(box, box.Faces[plusX]);
    BrushMesh flipped = BrushOps::FlipFace(box, plusX);

    // Winding reversed: both the recomputed and the cached normal point the other way.
    EXPECT_LT(BrushComputeFaceNormal(flipped, flipped.Faces[plusX]).Dot(original), -0.9f);
    EXPECT_LT(flipped.Faces[plusX].Normal.Dot(original), -0.9f);

    // Repair recomputes normals but must NOT re-orient, so the flip persists.
    BrushValidateAndRepair(flipped);
    EXPECT_LT(BrushComputeFaceNormal(flipped, flipped.Faces[plusX]).Dot(original), -0.9f);
}

TEST(BrushOps, ClipByAxisPlaneKeepsHalfAndCaps)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    // Plane x = 0, normal +X; keep the negative side (x <= 0).
    const Plane plane = Plane::FromNormalAndPoint({ 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
    const BrushMesh half = BrushOps::Clip(box, plane, /*keepPositiveSide*/ false);

    EXPECT_TRUE(IsClosed(half));
    EXPECT_TRUE(AllNormalsOutward(half));
    const Aabb3d bounds = BrushComputeBounds(half);
    EXPECT_FLOAT_EQ(bounds.Min.X, -1.0f);
    EXPECT_NEAR(bounds.Max.X, 0.0f, 1e-4f);
    EXPECT_EQ(half.Faces.size(), 6u);     // 5 clipped originals + 1 cap
    EXPECT_EQ(half.Vertices.size(), 8u);  // 4 at x=-1, 4 new at x=0
}

TEST(BrushOps, ClipByDiagonalPlaneStaysClosed)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const Plane plane = Plane::FromNormalAndPoint(
        Vec3d{ 1.0f, 1.0f, 0.0f }.Normalized(), { 0.0f, 0.0f, 0.0f });
    const BrushMesh half = BrushOps::Clip(box, plane, false);
    EXPECT_TRUE(IsClosed(half));
    EXPECT_TRUE(AllNormalsOutward(half));
    EXPECT_GT(half.Faces.size(), 4u);
}

namespace
{
    // A valid UV projection never points an axis along the face normal: both axes
    // lie in the face plane. The old extrude inherited the cap's projection onto
    // the perpendicular walls, which put an axis along the wall normal and
    // stretched the texture edge-on. This is the regression guard for that.
    bool AllUvAxesInFacePlanes(const BrushMesh& mesh)
    {
        for (const BrushFace& face : mesh.Faces)
        {
            const Vec3d n = BrushComputeFaceNormal(mesh, face).Normalized();
            const UvProjection& uv = face.Material.Uv;
            if (std::abs(uv.AxisU.Normalized().Dot(n)) > 1e-3f)
                return false;
            if (std::abs(uv.AxisV.Normalized().Dot(n)) > 1e-3f)
                return false;
        }
        return true;
    }
}

TEST(BrushOps, ExtrudeFaceWallsGetTheirOwnUvProjection)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    std::uint32_t plusX = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.9f)
            plusX = i;

    const BrushMesh extruded = BrushOps::ExtrudeFace(box, plusX, 2.0f);
    // Every face, including the new side walls, projects UVs in its own plane.
    EXPECT_TRUE(AllUvAxesInFacePlanes(extruded));
}

TEST(BrushOps, ExtrudeFaceAlongOffsetFollowsTheVector)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    std::uint32_t plusZ = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).Z > 0.9f)
            plusZ = i;

    // Offset is not along the face normal: the cap follows the vector.
    const BrushMesh extruded = BrushOps::ExtrudeFaceAlong(box, plusZ, { 0.0f, 0.0f, 3.0f });
    EXPECT_EQ(extruded.Vertices.size(), 12u);
    EXPECT_EQ(extruded.Faces.size(), 10u);
    EXPECT_FLOAT_EQ(BrushComputeBounds(extruded).Max.Z, 4.0f); // 1 + 3
    EXPECT_TRUE(AllUvAxesInFacePlanes(extruded));
}

TEST(BrushOps, ExtrudeEdgePullsOneNewPlane)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    BrushMesh pulled = BrushOps::ExtrudeEdge(box, a, b, { 0.0f, 0.0f, 3.0f });
    BrushValidateAndRepair(pulled); // ExtrudeEdge leaves validation to the caller
    EXPECT_EQ(pulled.Vertices.size(), 10u); // 8 + 2
    EXPECT_EQ(pulled.Faces.size(), 7u);     // 6 + 1 strip
    EXPECT_FALSE(IsClosed(pulled));         // a flap opens the mesh
}

TEST(BrushOps, ExtrudeEdgeZeroOffsetIsNoOp)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    BrushMesh pulled = BrushOps::ExtrudeEdge(box, a, b, { 0.0f, 0.0f, 0.0f });
    BrushValidateAndRepair(pulled); // welds the coincident ring away
    EXPECT_EQ(pulled.Vertices.size(), 8u);
    EXPECT_EQ(pulled.Faces.size(), 6u);
    EXPECT_TRUE(IsClosed(pulled));
}

TEST(BrushOps, MakePlaneIsOneFlatQuad)
{
    // Depth axis Y: the quad lies in X/Z with zero Y extent.
    const BrushMesh plane = BrushOps::MakePlane({ 2.0f, 1.0f, 3.0f }, /*depthAxis*/ 1);
    EXPECT_EQ(plane.Vertices.size(), 4u);
    ASSERT_EQ(plane.Faces.size(), 1u);
    EXPECT_EQ(plane.Faces[0].Loop.size(), 4u);

    const Aabb3d bounds = BrushComputeBounds(plane);
    EXPECT_FLOAT_EQ(bounds.Min.Y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Y, 0.0f); // flat: zero thickness on the depth axis
    EXPECT_FLOAT_EQ(bounds.Max.X, 2.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Z, 3.0f);
    // The face must point along +depthAxis (up), not flipped down: a single open
    // face is not reoriented by ValidateAndRepair, so MakePlane fixes it.
    EXPECT_GT(BrushComputeFaceNormal(plane, plane.Faces[0]).Y, 0.9f);
    // UV axes lie in the face plane (seeded like MakeBox).
    EXPECT_TRUE(AllUvAxesInFacePlanes(plane));
}

TEST(BrushOps, MakePlaneFacesUpForEveryDepthAxis)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const BrushMesh plane = BrushOps::MakePlane({ 1.0f, 1.0f, 1.0f }, axis);
        ASSERT_EQ(plane.Faces.size(), 1u);
        EXPECT_GT(BrushComputeFaceNormal(plane, plane.Faces[0])[axis], 0.9f);
    }
}

TEST(BrushOps, MakeCylinderIsClosedPrismAboutDepthAxis)
{
    const int sides = 8;
    const BrushMesh cyl = BrushOps::MakeCylinder({ 1.0f, 2.0f, 1.0f }, /*depthAxis*/ 1, sides);
    EXPECT_EQ(cyl.Vertices.size(), static_cast<std::size_t>(sides) * 2u); // welded top + bottom rings
    EXPECT_EQ(cyl.Faces.size(), static_cast<std::size_t>(sides) + 2u);    // sides + two caps
    EXPECT_TRUE(IsClosed(cyl));
    EXPECT_TRUE(AllNormalsOutward(cyl));

    const Aabb3d bounds = BrushComputeBounds(cyl);
    EXPECT_FLOAT_EQ(bounds.Min.Y, -2.0f); // height on the depth axis
    EXPECT_FLOAT_EQ(bounds.Max.Y, 2.0f);
    EXPECT_NEAR(bounds.Max.X, 1.0f, 1e-4f); // cross-section fills the footprint
    EXPECT_NEAR(bounds.Max.Z, 1.0f, 1e-4f);
}

TEST(BrushOps, MakeCylinderClampsSidesToThree)
{
    const BrushMesh cyl = BrushOps::MakeCylinder({ 1.0f, 1.0f, 1.0f }, /*depthAxis*/ 1, /*sides*/ 1);
    EXPECT_EQ(cyl.Faces.size(), 5u); // clamped to a triangular prism: 3 sides + 2 caps
    EXPECT_TRUE(IsClosed(cyl));
}

TEST(BrushOps, MakePrimitiveDispatchesToBox)
{
    BrushPrimitiveParams params{};
    params.HalfExtents = { 1.0f, 1.0f, 1.0f };
    const BrushMesh box = BrushOps::MakePrimitive(BrushPrimitive::Box, params);
    EXPECT_EQ(box.Faces.size(), 6u);
    EXPECT_TRUE(IsClosed(box));
}

TEST(BrushOps, InsertEdgeLoopCutsAtAuthoredPosition)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    const BrushMesh cut = BrushOps::InsertEdgeLoop(box, a, b, 0.25f);
    ASSERT_GT(cut.Vertices.size(), box.Vertices.size()); // the loop split some edges

    // A new vertex sits 25% from a toward b on the seed edge.
    const Vec3d expected = box.Vertices[a].Position * 0.75f + box.Vertices[b].Position * 0.25f;
    bool found = false;
    for (std::size_t i = box.Vertices.size(); i < cut.Vertices.size(); ++i)
        if ((cut.Vertices[i].Position - expected).SqrMagnitude() < 1.0e-8f) { found = true; break; }
    EXPECT_TRUE(found);

    // The original vertices are untouched.
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
    {
        EXPECT_FLOAT_EQ(cut.Vertices[i].Position.X, box.Vertices[i].Position.X);
        EXPECT_FLOAT_EQ(cut.Vertices[i].Position.Y, box.Vertices[i].Position.Y);
        EXPECT_FLOAT_EQ(cut.Vertices[i].Position.Z, box.Vertices[i].Position.Z);
    }
}

TEST(BrushOps, InsertEdgeLoopDefaultStaysMidpoint)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    const BrushMesh cut = BrushOps::InsertEdgeLoop(box, a, b);
    const Vec3d expectedMid = (box.Vertices[a].Position + box.Vertices[b].Position) * 0.5f;
    bool found = false;
    for (std::size_t i = box.Vertices.size(); i < cut.Vertices.size(); ++i)
        if ((cut.Vertices[i].Position - expectedMid).SqrMagnitude() < 1.0e-8f) { found = true; break; }
    EXPECT_TRUE(found);
}

TEST(BrushOps, InsertEdgeCutSplitsOnlyTheSeedFaces)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    BrushMesh cut = BrushOps::InsertEdgeCut(box, a, b, 0.3f);
    // The seed edge borders two quads; each splits into two (net +2 faces).
    EXPECT_EQ(cut.Faces.size(), box.Faces.size() + 2);

    const Vec3d expected = box.Vertices[a].Position * 0.7f + box.Vertices[b].Position * 0.3f;
    bool found = false;
    for (std::size_t i = box.Vertices.size(); i < cut.Vertices.size(); ++i)
        if ((cut.Vertices[i].Position - expected).SqrMagnitude() < 1.0e-8f) { found = true; break; }
    EXPECT_TRUE(found);

    // The open (T-junction) result is still usable after repair, like DeleteFace.
    const BrushRepairResult repair = BrushValidateAndRepair(cut);
    EXPECT_TRUE(repair.Ok);
}

TEST(BrushOps, InsertEdgeCutFaceFilterSplitsOnlyThatFace)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    // Restricted to face 0: only that quad splits (6 -> 7).
    const BrushMesh oneFace = BrushOps::InsertEdgeCut(box, a, b, 0.5f, /*faceIndex*/ 0);
    EXPECT_EQ(oneFace.Faces.size(), box.Faces.size() + 1);

    // Default still cuts both adjacent faces (6 -> 8).
    const BrushMesh bothFaces = BrushOps::InsertEdgeCut(box, a, b, 0.5f);
    EXPECT_EQ(bothFaces.Faces.size(), box.Faces.size() + 2);
}
