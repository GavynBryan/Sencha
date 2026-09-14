#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

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

TEST(BrushOps, AnOpenClipLeavesTheCutUncapped)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const Plane plane{ Vec3d{ 1.0f, 0.0f, 0.0f }, 0.0f };
    const BrushMesh capped = BrushOps::Clip(box, plane, false);
    const BrushMesh open = BrushOps::Clip(box, plane, false, BrushOps::ClipCap::Open);
    EXPECT_EQ(open.Faces.size(), capped.Faces.size() - 1);
    BrushMesh check = open;
    const BrushRepairResult report = BrushValidateAndRepair(check);
    EXPECT_TRUE(report.Ok);
    EXPECT_FALSE(report.Closed);
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

TEST(BrushOps, MakePlaneSubdividesIntoWeldedQuadGrid)
{
    const int n = 3;
    const BrushMesh plane = BrushOps::MakePlane({ 2.0f, 1.0f, 3.0f }, /*depthAxis*/ 1, n);
    EXPECT_EQ(plane.Faces.size(), static_cast<std::size_t>(n) * n);
    // Interior grid vertices are shared, not duplicated per quad.
    EXPECT_EQ(plane.Vertices.size(), static_cast<std::size_t>(n + 1) * (n + 1));

    const Aabb3d bounds = BrushComputeBounds(plane);
    EXPECT_FLOAT_EQ(bounds.Min.Y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.Max.X, 2.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Z, 3.0f);

    for (const BrushFace& face : plane.Faces)
        EXPECT_GT(BrushComputeFaceNormal(plane, face).Y, 0.9f);
    EXPECT_TRUE(AllUvAxesInFacePlanes(plane));
}

TEST(BrushOps, FlipAllFacesReversesEveryNormal)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const BrushMesh flipped = BrushOps::FlipAllFaces(box);
    ASSERT_EQ(flipped.Faces.size(), box.Faces.size());
    for (std::size_t i = 0; i < box.Faces.size(); ++i)
    {
        const Vec3d expected = -BrushComputeFaceNormal(box, box.Faces[i]);
        const Vec3d actual = BrushComputeFaceNormal(flipped, flipped.Faces[i]);
        EXPECT_GT(actual.Dot(expected), 0.99f);
    }
}

TEST(BrushOps, DissolveEdgeMergesTwoQuadsIntoOneHexagon)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    BrushMesh dissolved = BrushOps::DissolveEdge(box, a, b);
    ASSERT_EQ(dissolved.Faces.size(), 5u);
    EXPECT_EQ(dissolved.Vertices.size(), 8u); // endpoints stay in the merged loop
    EXPECT_EQ(dissolved.Faces.back().Loop.size(), 6u);
    EXPECT_TRUE(IsClosed(dissolved));
}

TEST(BrushOps, DissolveEdgeRefusesBoundaryAndBogusEdges)
{
    const BrushMesh plane = BrushOps::MakePlane({ 1.0f, 0.0f, 1.0f }, 1);
    // Every plane edge borders exactly one face: nothing to merge.
    const BrushMesh unchanged =
        BrushOps::DissolveEdge(plane, plane.Faces[0].Loop[0], plane.Faces[0].Loop[1]);
    EXPECT_EQ(unchanged.Faces.size(), plane.Faces.size());

    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    EXPECT_EQ(BrushOps::DissolveEdge(box, 0, 0).Faces.size(), 6u);
    EXPECT_EQ(BrushOps::DissolveEdge(box, 0, 99).Faces.size(), 6u);
}

TEST(BrushOps, DissolveEdgeRemovesCollinearEndpointsFromCoplanarQuads)
{
    BrushMesh grid;
    grid.Vertices = {
        { { 0.0f, 0.0f, 0.0f } }, { { 1.0f, 0.0f, 0.0f } },
        { { 2.0f, 0.0f, 0.0f } }, { { 0.0f, 0.0f, 1.0f } },
        { { 1.0f, 0.0f, 1.0f } }, { { 2.0f, 0.0f, 1.0f } },
    };
    grid.Faces = {
        BrushFace{ .Loop = { 0, 1, 4, 3 } },
        BrushFace{ .Loop = { 1, 2, 5, 4 } },
    };
    const std::uint32_t a = 1;
    const std::uint32_t b = 4;

    BrushMesh dissolved = BrushOps::DissolveEdge(grid, a, b);
    ASSERT_EQ(dissolved.Faces.size(), 1u);
    EXPECT_EQ(dissolved.Faces.back().Loop.size(), 4u);
    ASSERT_TRUE(BrushValidateAndRepair(dissolved).Ok);
    EXPECT_EQ(dissolved.Vertices.size(), grid.Vertices.size() - 2u);
}

TEST(BrushOps, WeldVerticesCollapsesClustersToCentroid)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::vector<std::uint32_t> all;
    for (std::uint32_t i = 0; i < box.Vertices.size(); ++i)
        all.push_back(i);

    // Every corner is within 10 of every other: one cluster at the centroid.
    const BrushMesh welded = BrushOps::WeldVertices(box, all, 10.0f);
    for (const BrushVertex& vertex : welded.Vertices)
        EXPECT_LT(vertex.Position.SqrMagnitude(), 1e-8);

    // Nothing is within 0.1 of anything else: untouched.
    const BrushMesh untouched = BrushOps::WeldVertices(box, all, 0.1f);
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
        EXPECT_LT((untouched.Vertices[i].Position - box.Vertices[i].Position).SqrMagnitude(), 1e-12);
}

namespace
{
    // Two coplanar floor quads (+Y) with a 2-unit gap along X: quad A spans
    // x [-2,-1], quad B x [1,2], both z [-1,1]. A's right edge is verts {1,2},
    // B's left edge is verts {4,7}.
    BrushMesh TwoFloorQuads()
    {
        BrushMesh mesh;
        mesh.Vertices = {
            { { -2.0f, 0.0f, -1.0f } }, { { -1.0f, 0.0f, -1.0f } },
            { { -1.0f, 0.0f, 1.0f } },  { { -2.0f, 0.0f, 1.0f } },
            { { 1.0f, 0.0f, -1.0f } },  { { 2.0f, 0.0f, -1.0f } },
            { { 2.0f, 0.0f, 1.0f } },   { { 1.0f, 0.0f, 1.0f } },
        };
        BrushFace a;
        a.Loop = { 0, 3, 2, 1 };
        a.Normal = BrushComputeFaceNormal(mesh, a);
        BrushFace b;
        b.Loop = { 4, 7, 6, 5 };
        b.Normal = BrushComputeFaceNormal(mesh, b);
        mesh.Faces = { a, b };
        return mesh;
    }
}

TEST(BrushOps, BridgeEdgePathsSpansCoplanarQuadsStraight)
{
    const BrushMesh mesh = TwoFloorQuads();
    const std::uint32_t pathA[] = { 1, 2 };
    const std::uint32_t pathB[] = { 4, 7 };

    const BrushMesh one = BrushOps::BridgeEdgePaths(mesh, pathA, pathB, 1);
    EXPECT_EQ(one.Faces.size(), 3u);
    EXPECT_EQ(one.Vertices.size(), 8u); // single quad reuses existing corners

    // Coplanar boundary tangents both align with the chord: the multi-segment
    // bridge stays flat on the floor plane.
    const BrushMesh four = BrushOps::BridgeEdgePaths(mesh, pathA, pathB, 4);
    EXPECT_EQ(four.Faces.size(), 2u + 4u);
    ASSERT_EQ(four.Vertices.size(), 8u + 6u); // 3 interior rows x 2 columns
    for (std::size_t i = 8; i < four.Vertices.size(); ++i)
        EXPECT_NEAR(four.Vertices[i].Position.Y, 0.0f, 1e-5);
}

TEST(BrushOps, BridgeEdgePathsBowsBetweenPerpendicularFaces)
{
    // Floor quad A plus a vertical wall whose bottom edge faces A's right edge.
    BrushMesh mesh = TwoFloorQuads();
    mesh.Faces.pop_back(); // drop quad B; its verts 4 and 7 anchor the wall
    mesh.Vertices.push_back({ { 1.0f, 2.0f, 1.0f } });  // 8
    mesh.Vertices.push_back({ { 1.0f, 2.0f, -1.0f } }); // 9
    BrushFace wall;
    wall.Loop = { 4, 7, 8, 9 };
    wall.Normal = BrushComputeFaceNormal(mesh, wall);
    mesh.Faces.push_back(wall);

    const std::uint32_t pathA[] = { 1, 2 };
    const std::uint32_t pathB[] = { 4, 7 };
    const BrushMesh bridged = BrushOps::BridgeEdgePaths(mesh, pathA, pathB, 4);
    ASSERT_EQ(bridged.Vertices.size(), 10u + 6u);
    EXPECT_EQ(bridged.Faces.size(), 2u + 4u);

    // The blend leaves the floor flat and arrives climbing the wall, so the
    // interior rows must leave the chord (y != 0 somewhere).
    bool offChord = false;
    for (std::size_t i = 10; i < bridged.Vertices.size(); ++i)
        offChord |= std::abs(bridged.Vertices[i].Position.Y) > 1e-3;
    EXPECT_TRUE(offChord);
}

TEST(BrushOps, BridgeEdgePathsRefusesMismatchedPaths)
{
    const BrushMesh mesh = TwoFloorQuads();
    const std::uint32_t pathA[] = { 1, 2 };
    const std::uint32_t shared[] = { 2, 3 };
    const std::uint32_t longPath[] = { 4, 7, 6 };
    EXPECT_EQ(BrushOps::BridgeEdgePaths(mesh, pathA, longPath, 1).Faces.size(), 2u);
    EXPECT_EQ(BrushOps::BridgeEdgePaths(mesh, pathA, shared, 1).Faces.size(), 2u);
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

TEST(BrushOps, InsertEdgeCutFaceFilterTurnsNeighbouringQuadsIntoNgons)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];

    BrushMesh cut = BrushOps::InsertEdgeCut(box, a, b, 0.5f, /*faceIndex*/ 0);
    EXPECT_EQ(cut.Faces.size(), box.Faces.size() + 1);
    int ngons = 0;
    for (const BrushFace& face : cut.Faces)
        ngons += face.Loop.size() == 5;
    EXPECT_GE(ngons, 2);
    EXPECT_TRUE(BrushValidateAndRepair(cut).Ok);
    EXPECT_TRUE(IsClosed(cut));
}

//=============================================================================
// Face carve: rect-quad frame query + the carve topology generator.
//=============================================================================

namespace
{
    // The +Z face of a unit-half box (a 2x2 rectangular quad) and its frame.
    struct CarveHost
    {
        BrushMesh Mesh;
        std::uint32_t Face = 0;
        BrushOps::BrushRectFaceFrame Frame{};
    };

    CarveHost MakeCarveHost()
    {
        CarveHost host;
        host.Mesh = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
        for (std::uint32_t i = 0; i < host.Mesh.Faces.size(); ++i)
            if (BrushComputeFaceNormal(host.Mesh, host.Mesh.Faces[i]).Z > 0.9f)
                host.Face = i;
        host.Frame = *BrushOps::RectFaceFrame(host.Mesh, host.Face);
        return host;
    }

    std::size_t CountFrameFaceCentroidsInside(const BrushMesh& mesh,
                                              const BrushOps::BrushRectFaceFrame& frame,
                                              Vec2d rectMin, Vec2d rectMax)
    {
        const float u0 = std::min(rectMin.X, rectMax.X);
        const float u1 = std::max(rectMin.X, rectMax.X);
        const float v0 = std::min(rectMin.Y, rectMax.Y);
        const float v1 = std::max(rectMin.Y, rectMax.Y);
        const Vec3d normal = frame.AxisU.Cross(frame.AxisV);

        std::size_t count = 0;
        for (const BrushFace& face : mesh.Faces)
        {
            if (BrushComputeFaceNormal(mesh, face).Dot(normal) < 0.9f)
                continue;
            const Vec3d rel = BrushFaceCentroid(mesh, face) - frame.Origin;
            if (std::abs(rel.Dot(normal)) > 1.0e-3f)
                continue;
            const Vec2d uv{ rel.Dot(frame.AxisU), rel.Dot(frame.AxisV) };
            if (uv.X > u0 + 1.0e-3f && uv.X < u1 - 1.0e-3f
                && uv.Y > v0 + 1.0e-3f && uv.Y < v1 - 1.0e-3f)
                ++count;
        }
        return count;
    }

}

TEST(BrushOpsCarve, RectFaceFrameAcceptsBoxFaceRejectsOthers)
{
    const CarveHost host = MakeCarveHost();
    EXPECT_FLOAT_EQ(host.Frame.Width, 2.0f);
    EXPECT_FLOAT_EQ(host.Frame.Height, 2.0f);
    EXPECT_NEAR(host.Frame.AxisU.Dot(host.Frame.AxisV), 0.0f, 1e-3f);
    // For the CCW outward loop, U x V is the outward (+Z-ish) normal.
    const Vec3d normal = BrushComputeFaceNormal(host.Mesh, host.Mesh.Faces[host.Face]);
    EXPECT_GT(host.Frame.AxisU.Cross(host.Frame.AxisV).Dot(normal), 0.99f);

    // Cylinder cap is an n-gon: rejected.
    const BrushMesh cylinder = BrushOps::MakeCylinder({ 1.0f, 1.0f, 1.0f }, 1, 8);
    bool anyCap = false;
    for (std::uint32_t i = 0; i < cylinder.Faces.size(); ++i)
        if (cylinder.Faces[i].Loop.size() > 4)
        {
            EXPECT_FALSE(BrushOps::RectFaceFrame(cylinder, i).has_value());
            anyCap = true;
        }
    EXPECT_TRUE(anyCap);

    // Sheared quad: rejected.
    BrushMesh sheared = BrushOps::MakePlane({ 1.0f, 0.0f, 1.0f }, 1);
    sheared.Vertices[sheared.Faces[0].Loop[1]].Position += Vec3d{ 0.0f, 0.0f, 0.4f };
    EXPECT_FALSE(BrushOps::RectFaceFrame(sheared, 0).has_value());
}

TEST(BrushOpsCarve, InsertFaceLoopBoundsCreatesClosedBoundedRegion)
{
    CarveHost host = MakeCarveHost();
    BrushMesh out = BrushOps::InsertFaceLoopBounds(
        host.Mesh, host.Face, { 0.5f, 0.5f }, { 1.5f, 1.5f });

    EXPECT_GT(out.Vertices.size(), host.Mesh.Vertices.size());
    EXPECT_GT(out.Faces.size(), host.Mesh.Faces.size());
    EXPECT_TRUE(BrushValidateAndRepair(out).Ok);
    EXPECT_TRUE(IsClosed(out));
    EXPECT_EQ(CountFrameFaceCentroidsInside(out, host.Frame, { 0.5f, 0.5f }, { 1.5f, 1.5f }), 1u);
}

TEST(BrushOpsCarve, InsertFaceLoopBoundsOmitsFlushSides)
{
    CarveHost host = MakeCarveHost();
    BrushMesh out = BrushOps::InsertFaceLoopBounds(
        host.Mesh, host.Face, { 0.0f, 0.5f }, { 1.5f, 1.5f });

    EXPECT_GT(out.Vertices.size(), host.Mesh.Vertices.size());
    EXPECT_GT(out.Faces.size(), host.Mesh.Faces.size());
    EXPECT_TRUE(BrushValidateAndRepair(out).Ok);
    EXPECT_TRUE(IsClosed(out));
    EXPECT_GE(CountFrameFaceCentroidsInside(out, host.Frame, { 0.0f, 0.5f }, { 1.5f, 1.5f }), 1u);
}

TEST(BrushOpsCarve, InsertFaceLoopBoundsRejectsFullCoverAndInvalidFace)
{
    CarveHost host = MakeCarveHost();

    EXPECT_EQ(BrushOps::InsertFaceLoopBounds(host.Mesh, host.Face,
                                             { 0.0f, 0.0f }, { 2.0f, 2.0f }).Faces.size(),
              host.Mesh.Faces.size());
    EXPECT_EQ(BrushOps::InsertFaceLoopBounds(host.Mesh, 99,
                                             { 0.5f, 0.5f }, { 1.5f, 1.5f }).Faces.size(),
              host.Mesh.Faces.size());
}

TEST(BrushOpsCarve, ThroughLoopBoundsCutTunnelBetweenOppositeFaces)
{
    CarveHost host = MakeCarveHost();
    BrushMesh out = BrushOps::InsertFaceLoopBoundsThrough(
        host.Mesh, host.Face, { 0.5f, 0.5f }, { 1.5f, 1.5f });

    ASSERT_GT(out.Faces.size(), host.Mesh.Faces.size());
    EXPECT_TRUE(BrushValidateAndRepair(out).Ok);
    EXPECT_TRUE(IsClosed(out));

    // A through-hole makes the solid a torus: V - E + F == 0.
    std::set<std::pair<std::uint32_t, std::uint32_t>> undirected;
    for (const BrushFace& face : out.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
            undirected.insert({ std::min(a, b), std::max(a, b) });
        }
    const std::ptrdiff_t euler = static_cast<std::ptrdiff_t>(out.Vertices.size())
        - static_cast<std::ptrdiff_t>(undirected.size())
        + static_cast<std::ptrdiff_t>(out.Faces.size());
    EXPECT_EQ(euler, 0);

    // The openings are open: no face centroid inside the rect on the source
    // plane, nor inside the projected rect on the opposite plane.
    EXPECT_EQ(CountFrameFaceCentroidsInside(out, host.Frame, { 0.5f, 0.5f }, { 1.5f, 1.5f }), 0u);
}

TEST(BrushOpsCarve, ThroughLoopBoundsRejectOppositePairFlushAndFullCover)
{
    CarveHost host = MakeCarveHost();
    // Opposite-pair-only channel would split the brush in two: refused.
    const BrushMesh channel = BrushOps::InsertFaceLoopBoundsThrough(
        host.Mesh, host.Face, { 0.0f, 0.5f }, { 2.0f, 1.5f });
    EXPECT_EQ(channel.Faces.size(), host.Mesh.Faces.size());
    EXPECT_EQ(channel.Vertices.size(), host.Mesh.Vertices.size());
    // Full cover removes everything: refused.
    const BrushMesh cover = BrushOps::InsertFaceLoopBoundsThrough(
        host.Mesh, host.Face, { 0.0f, 0.0f }, { 2.0f, 2.0f });
    EXPECT_EQ(cover.Faces.size(), host.Mesh.Faces.size());
    EXPECT_EQ(cover.Vertices.size(), host.Mesh.Vertices.size());
}

namespace
{
    // Square loop of side 2 at the given height, counterclockwise viewed from
    // +Y (Newell normal +Y).
    std::vector<Vec3d> SquareLoopCcw(float y)
    {
        return {
            { 1.0f, y, 1.0f },
            { -1.0f, y, 1.0f },
            { -1.0f, y, -1.0f },
            { 1.0f, y, -1.0f },
        };
    }

    // No directed edge may repeat: every quad traverses shared edges in
    // opposite directions, which is exactly what a twisted or flipped strip
    // violates.
    bool DirectedEdgesConsistent(const BrushMesh& mesh)
    {
        std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        for (const BrushFace& face : mesh.Faces)
            for (std::size_t i = 0; i < face.Loop.size(); ++i)
            {
                const std::uint32_t a = face.Loop[i];
                const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
                if (!seen.insert({ a, b }).second)
                    return false;
            }
        return true;
    }

    // Every bridge column between the two rings must connect vertically
    // aligned corners; a crossed (twisted) pairing breaks this.
    void ExpectColumnsVertical(const BrushMesh& tube, std::size_t n)
    {
        for (const BrushFace& face : tube.Faces)
            for (std::size_t i = 0; i < face.Loop.size(); ++i)
            {
                const std::uint32_t a = face.Loop[i];
                const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
                const bool rowA = a < n;
                const bool rowB = b < n;
                if (rowA == rowB)
                    continue; // a ring edge, not a column
                const Vec3d pa = tube.Vertices[a].Position;
                const Vec3d pb = tube.Vertices[b].Position;
                EXPECT_NEAR(pa.X, pb.X, 1e-5);
                EXPECT_NEAR(pa.Z, pb.Z, 1e-5);
            }
    }
}

TEST(BrushOps, PathBoundaryTangentsClosedCapLoopAllNonZero)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const BrushFace* top = nullptr;
    for (const BrushFace& face : box.Faces)
        if (face.Normal.Y > 0.9f)
            top = &face;
    ASSERT_NE(top, nullptr);

    const auto tangents = BrushOps::PathBoundaryTangents(box, top->Loop, /*closed*/ true);
    ASSERT_EQ(tangents.size(), top->Loop.size());
    for (std::size_t i = 0; i < tangents.size(); ++i)
    {
        // A rim vertex departs along the cap/side corner bisector: up and
        // radially outward, never zero.
        EXPECT_GT(tangents[i].Magnitude(), 0.5f);
        EXPECT_GT(tangents[i].Y, 0.1f);
        const Vec3d p = box.Vertices[top->Loop[i]].Position;
        EXPECT_GT(tangents[i].X * p.X + tangents[i].Z * p.Z, 0.0f);
    }
}

TEST(BrushOps, BuildBridgeBetweenPathsDeterminesReversalByWinding)
{
    // Loop B winds the opposite way and starts elsewhere: the symmetric square
    // makes distance tie between pairings, so only the winding rule can decide
    // reversal. The result must be an untwisted, consistently wound tube.
    BrushOps::BridgePathSpec a;
    a.Positions = SquareLoopCcw(0.0f);
    a.Closed = true;
    BrushOps::BridgePathSpec b;
    b.Positions = SquareLoopCcw(4.0f);
    std::reverse(b.Positions.begin(), b.Positions.end());
    std::rotate(b.Positions.begin(), b.Positions.begin() + 2, b.Positions.end());
    b.Closed = true;

    const BrushMesh tube = BrushOps::BuildBridgeBetweenPaths(a, b, 1, nullptr);
    ASSERT_EQ(tube.Faces.size(), 4u);
    ASSERT_EQ(tube.Vertices.size(), 8u);
    EXPECT_TRUE(DirectedEdgesConsistent(tube));
    ExpectColumnsVertical(tube, 4);

    // Walls face outward, away from the tube axis.
    for (const BrushFace& face : tube.Faces)
    {
        Vec3d centroid = {};
        for (std::uint32_t v : face.Loop)
            centroid += tube.Vertices[v].Position;
        centroid = centroid * 0.25;
        EXPECT_GT(face.Normal.X * centroid.X + face.Normal.Z * centroid.Z, 0.1f);
    }
}

TEST(BrushOps, BuildBridgeBetweenPathsClosedLoopRotationAligned)
{
    BrushOps::BridgePathSpec a;
    a.Positions = SquareLoopCcw(0.0f);
    a.Closed = true;
    BrushOps::BridgePathSpec b;
    b.Positions = SquareLoopCcw(4.0f);
    std::rotate(b.Positions.begin(), b.Positions.begin() + 3, b.Positions.end());
    b.Closed = true;

    const BrushMesh tube = BrushOps::BuildBridgeBetweenPaths(a, b, 1, nullptr);
    ASSERT_EQ(tube.Faces.size(), 4u);
    EXPECT_TRUE(DirectedEdgesConsistent(tube));
    ExpectColumnsVertical(tube, 4);
}

TEST(BrushOps, BuildBridgeBetweenPathsBowsWithSegments)
{
    BrushOps::BridgePathSpec a;
    a.Positions = SquareLoopCcw(0.0f);
    a.Closed = true;
    BrushOps::BridgePathSpec b;
    b.Positions = SquareLoopCcw(4.0f);
    b.Closed = true;
    // Radially outward departure tangents on both rims, as a box rim yields.
    for (const Vec3d& p : a.Positions)
        a.Tangents.push_back(Vec3d(p.X, 0.0f, p.Z).Normalized());
    b.Tangents = a.Tangents;

    const BrushMesh tube = BrushOps::BuildBridgeBetweenPaths(a, b, 4, nullptr);
    ASSERT_EQ(tube.Vertices.size(), 8u + 12u); // 3 interior rows x 4 columns
    EXPECT_EQ(tube.Faces.size(), 16u);

    // Interior rows bulge outward past the straight lerp of the rims.
    for (std::size_t i = 8; i < tube.Vertices.size(); ++i)
    {
        const Vec3d p = tube.Vertices[i].Position;
        EXPECT_GT(std::sqrt(p.X * p.X + p.Z * p.Z), std::sqrt(2.0) + 0.05);
    }
}

TEST(BrushOps, BuildBridgeBetweenPathsRefusesUnpairablePaths)
{
    BrushOps::BridgePathSpec a;
    a.Positions = SquareLoopCcw(0.0f);
    a.Closed = true;
    BrushOps::BridgePathSpec shorter;
    shorter.Positions = { { 0.0f, 4.0f, 0.0f }, { 1.0f, 4.0f, 0.0f }, { 2.0f, 4.0f, 0.0f } };
    shorter.Closed = true;
    EXPECT_TRUE(BrushOps::BuildBridgeBetweenPaths(a, shorter, 1, nullptr).Faces.empty());

    BrushOps::BridgePathSpec open;
    open.Positions = SquareLoopCcw(4.0f);
    open.Closed = false;
    EXPECT_TRUE(BrushOps::BuildBridgeBetweenPaths(a, open, 1, nullptr).Faces.empty());
}

namespace
{
    // V - E + F over the faces actually present (2 = sphere-like, 0 = torus).
    std::ptrdiff_t EulerCharacteristic(const BrushMesh& mesh)
    {
        std::set<std::uint32_t> referenced;
        std::set<std::pair<std::uint32_t, std::uint32_t>> undirected;
        for (const BrushFace& face : mesh.Faces)
            for (std::size_t i = 0; i < face.Loop.size(); ++i)
            {
                const std::uint32_t a = face.Loop[i];
                const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
                referenced.insert(a);
                undirected.insert({ std::min(a, b), std::max(a, b) });
            }
        return static_cast<std::ptrdiff_t>(referenced.size())
             - static_cast<std::ptrdiff_t>(undirected.size())
             + static_cast<std::ptrdiff_t>(mesh.Faces.size());
    }
}

TEST(BrushOpsCarve, ThroughLoopBoundsFlushSideOpensNotch)
{
    CarveHost host = MakeCarveHost();
    BrushMesh out = BrushOps::InsertFaceLoopBoundsThrough(
        host.Mesh, host.Face, { 0.5f, 0.0f }, { 1.5f, 1.0f });

    ASSERT_NE(out.Faces.size(), host.Mesh.Faces.size());
    EXPECT_TRUE(BrushValidateAndRepair(out).Ok);
    EXPECT_TRUE(IsClosed(out));
    EXPECT_EQ(EulerCharacteristic(out), 2); // an open notch is still genus 0
    EXPECT_EQ(CountFrameFaceCentroidsInside(out, host.Frame, { 0.5f, 0.0f }, { 1.5f, 1.0f }), 0u);
}

TEST(BrushOpsCarve, LoopBoundsCoincidingWithExistingLoopSkipThatCut)
{
    CarveHost host = MakeCarveHost();
    // First loop pair establishes cuts at u = 0.5 and u = 1.5.
    BrushMesh once = BrushOps::InsertFaceLoopBounds(
        host.Mesh, host.Face, { 0.5f, 0.5f }, { 1.5f, 1.5f });
    ASSERT_GT(once.Faces.size(), host.Mesh.Faces.size());

    // A second rect sharing the u = 0.5 bound must not fail on it: only the
    // genuinely new bounds cut.
    std::uint32_t face = 0;
    bool found = false;
    for (std::uint32_t f = 0; f < once.Faces.size() && !found; ++f)
        if (BrushOps::RectFaceFrame(once, f).has_value())
        {
            // Re-find the +Z host region: any face on the original frame plane.
            const Vec3d normal = host.Frame.AxisU.Cross(host.Frame.AxisV);
            const Vec3d rel = once.Vertices[once.Faces[f].Loop[0]].Position - host.Frame.Origin;
            if (std::abs(rel.Dot(normal)) < 1e-4f
                && BrushComputeFaceNormal(once, once.Faces[f]).Z > 0.9f)
            {
                face = f;
                found = true;
            }
        }
    ASSERT_TRUE(found);

    const std::optional<BrushOps::BrushRectFaceFrame> frame = BrushOps::RectFaceFrame(once, face);
    ASSERT_TRUE(frame.has_value());
    // The sub-face's own frame: cut at its interior; one side of the rect sits
    // exactly on the sub-face boundary minted by the first loop (an existing
    // edge), which InteriorFrameBounds drops as flush in this frame. The real
    // coincidence test is at whole-face scope below.
    BrushMesh twice = BrushOps::InsertFaceLoopBounds(
        once, face, { 0.0f, 0.2f }, { frame->Width * 0.5f, 0.8f });
    EXPECT_GT(twice.Vertices.size(), once.Vertices.size());
    EXPECT_TRUE(BrushValidateAndRepair(twice).Ok);
    EXPECT_TRUE(IsClosed(twice));
}

namespace
{
}

TEST(BrushOpsCarve, LoopBoundsWorkOnASubdividedPlane)
{
    const BrushMesh plane = BrushOps::MakePlane({ 2.0f, 0.0f, 2.0f }, /*depthAxis*/ 1, 2);
    ASSERT_EQ(plane.Faces.size(), 4u);

    BrushMesh out = BrushOps::InsertFaceLoopBounds(plane, 0, { 0.5f, 0.5f }, { 1.5f, 1.5f });
    EXPECT_GT(out.Vertices.size(), plane.Vertices.size());
    EXPECT_GT(out.Faces.size(), plane.Faces.size());
    EXPECT_TRUE(BrushValidateAndRepair(out).Ok);
}

TEST(BrushOpsInset, SingleFaceMatchesExtrudeShape)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::uint32_t plusZ = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).Z > 0.9f)
            plusZ = i;

    const std::uint32_t region[] = { plusZ };
    BrushMesh out = BrushOps::ExtrudeFacesAlongNormals(box, region, 0.5f);
    EXPECT_EQ(out.Vertices.size(), 12u);
    EXPECT_EQ(out.Faces.size(), 10u);
    EXPECT_TRUE(IsClosed(out));
    EXPECT_NEAR(BrushComputeBounds(out).Max.Z, 1.5f, 1e-5f);
    EXPECT_TRUE(AllUvAxesInFacePlanes(out));

    // Negative distance pushes the cap inward instead.
    BrushMesh inward = BrushOps::ExtrudeFacesAlongNormals(box, region, -0.5f);
    EXPECT_TRUE(IsClosed(inward));
    EXPECT_NEAR(BrushComputeBounds(inward).Max.Z, 1.0f, 1e-5f);
}

TEST(BrushOpsInset, AdjacentRingExtrudesAsOneShell)
{
    // All four side faces of a box: a closed band. The band inflates as one
    // shell; walls appear only along the top and bottom rims.
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::vector<std::uint32_t> band;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (std::abs(BrushComputeFaceNormal(box, box.Faces[i]).Y) < 0.1f)
            band.push_back(i);
    ASSERT_EQ(band.size(), 4u);

    BrushMesh out = BrushOps::ExtrudeFacesAlongNormals(box, band, 0.25f);
    EXPECT_TRUE(IsClosed(out));
    // 8 original + 8 moved ring verts; 6 faces + 8 rim walls.
    EXPECT_EQ(out.Vertices.size(), 16u);
    EXPECT_EQ(out.Faces.size(), 14u);
    // Corners move diagonally outward: X/Z bounds grow, Y stays.
    const Aabb3d bounds = BrushComputeBounds(out);
    EXPECT_GT(bounds.Max.X, 1.05f);
    EXPECT_GT(bounds.Max.Z, 1.05f);
    EXPECT_NEAR(bounds.Max.Y, 1.0f, 1e-5f);
}

TEST(BrushOpsInset, InvalidInputsAreNoOps)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t bogus[] = { 99u };
    EXPECT_EQ(BrushOps::ExtrudeFacesAlongNormals(box, bogus, 1.0f).Vertices.size(),
              box.Vertices.size());
    const std::uint32_t valid[] = { 0u };
    EXPECT_EQ(BrushOps::ExtrudeFacesAlongNormals(box, valid, 0.0f).Vertices.size(),
              box.Vertices.size());
}

TEST(BrushOpsBevel, SingleBoxEdgeChamfers)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    // Pick any edge: every box edge is manifold with perpendicular faces.
    const std::array<std::uint32_t, 2> edge = { box.Faces[0].Loop[0], box.Faces[0].Loop[1] };
    const std::array<std::uint32_t, 2> edges[] = { edge };

    BrushMesh out = BrushOps::BevelEdges(box, edges, 0.25f, 1);
    EXPECT_TRUE(IsClosed(out));
    // The edge's two vertices are replaced by 2 rows of 2: 8 - 2 + 4 = 10.
    EXPECT_EQ(out.Vertices.size(), 10u);
    // One chamfer strip added: 6 + 1 = 7.
    EXPECT_EQ(out.Faces.size(), 7u);
    // World-aligned box mapping on a 45-degree strip legitimately tilts an
    // axis out of the face plane; the degenerate case (an axis ALONG the
    // normal, projecting the texture edge-on) is what must never happen.
    for (const BrushFace& face : out.Faces)
    {
        const Vec3d n = BrushComputeFaceNormal(out, face).Normalized();
        EXPECT_LT(std::abs(face.Material.Uv.AxisU.Normalized().Dot(n)), 0.9f);
        EXPECT_LT(std::abs(face.Material.Uv.AxisV.Normalized().Dot(n)), 0.9f);
    }
}

TEST(BrushOpsBevel, SegmentsRoundTheProfile)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::array<std::uint32_t, 2> edge = { box.Faces[0].Loop[0], box.Faces[0].Loop[1] };
    const std::array<std::uint32_t, 2> edges[] = { edge };

    BrushMesh out = BrushOps::BevelEdges(box, edges, 0.25f, 3);
    EXPECT_TRUE(IsClosed(out));
    // 2 vertices -> 2 rows of 4: 8 - 2 + 8 = 14; 3 strip quads: 6 + 3 = 9.
    EXPECT_EQ(out.Vertices.size(), 14u);
    EXPECT_EQ(out.Faces.size(), 9u);
}

TEST(BrushOpsBevel, TopRimLoopBevelsWatertight)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::uint32_t top = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).Y > 0.9f)
            top = i;
    const std::vector<std::uint32_t>& loop = box.Faces[top].Loop;
    std::vector<std::array<std::uint32_t, 2>> rim;
    for (std::size_t i = 0; i < loop.size(); ++i)
        rim.push_back({ loop[i], loop[(i + 1) % loop.size()] });

    BrushMesh out = BrushOps::BevelEdges(box, rim, 0.25f, 2);
    EXPECT_TRUE(IsClosed(out));
    EXPECT_GT(out.Faces.size(), box.Faces.size());
    // The rim is gone: no vertex sits on both the top plane and a side plane.
    for (const BrushVertex& v : out.Vertices)
    {
        const bool onTop = std::abs(v.Position.Y - 1.0f) < 1e-4f;
        const bool onSide = std::abs(std::abs(v.Position.X) - 1.0f) < 1e-4f
                         || std::abs(std::abs(v.Position.Z) - 1.0f) < 1e-4f;
        EXPECT_FALSE(onTop && onSide);
    }
}

TEST(BrushOpsBevel, RefusesCornersAndCoplanarAndBoundary)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    // Three edges fanning one corner vertex: refused.
    const std::uint32_t corner = box.Faces[0].Loop[0];
    std::vector<std::array<std::uint32_t, 2>> fan;
    for (const BrushFace& face : box.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
            if (a == corner || b == corner)
            {
                std::array<std::uint32_t, 2> e = { std::min(a, b), std::max(a, b) };
                if (std::find(fan.begin(), fan.end(), e) == fan.end())
                    fan.push_back(e);
            }
        }
    ASSERT_GE(fan.size(), 3u);
    EXPECT_EQ(BrushOps::BevelEdges(box, fan, 0.2f, 1).Vertices.size(), box.Vertices.size());

    // A boundary edge on a plane (single face): refused.
    const BrushMesh plane = BrushOps::MakePlane({ 1.0f, 0.0f, 1.0f }, 1);
    const std::array<std::uint32_t, 2> planeEdge[] = {
        { plane.Faces[0].Loop[0], plane.Faces[0].Loop[1] } };
    EXPECT_EQ(BrushOps::BevelEdges(plane, planeEdge, 0.2f, 1).Vertices.size(),
              plane.Vertices.size());

    // A coplanar interior edge (subdivided plane): refused. Find an edge
    // shared by exactly two (coplanar) quads.
    const BrushMesh grid = BrushOps::MakePlane({ 2.0f, 0.0f, 2.0f }, 1, 2);
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> counts;
    for (const BrushFace& face : grid.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
            ++counts[{ std::min(a, b), std::max(a, b) }];
        }
    std::array<std::uint32_t, 2> interior = { 0, 0 };
    for (const auto& [key, count] : counts)
        if (count == 2)
            interior = { key.first, key.second };
    ASSERT_NE(interior[0], interior[1]);
    const std::array<std::uint32_t, 2> interiorEdges[] = { interior };
    EXPECT_EQ(BrushOps::BevelEdges(grid, interiorEdges, 0.2f, 1).Vertices.size(),
              grid.Vertices.size());
}

TEST(BrushOpsInset, DirectionalRegionExtrudeWallsOnlyTheBoundary)
{
    // Two coplanar faces of a subdivided slab extruded together: the shared
    // edge must stay internal (no swept wall between the caps).
    BrushMesh slab = BrushOps::MakeBox({ 2.0f, 1.0f, 1.0f });
    std::uint32_t top = 0;
    for (std::uint32_t i = 0; i < slab.Faces.size(); ++i)
        if (BrushComputeFaceNormal(slab, slab.Faces[i]).Y > 0.9f)
            top = i;
    const std::vector<std::uint32_t>& loop = slab.Faces[top].Loop;
    slab = BrushOps::InsertEdgeCut(slab, loop[0], loop[1], 0.5f);
    BrushValidateAndRepair(slab);

    std::vector<std::uint32_t> caps;
    for (std::uint32_t i = 0; i < slab.Faces.size(); ++i)
        if (BrushComputeFaceNormal(slab, slab.Faces[i]).Y > 0.9f)
            caps.push_back(i);
    ASSERT_EQ(caps.size(), 2u);

    const BrushMesh out = BrushOps::ExtrudeFacesAlong(slab, caps, { 0.0f, 0.5f, 0.0f });
    ASSERT_GT(out.Faces.size(), slab.Faces.size());
    EXPECT_TRUE(IsClosed(out));

    // No face may sit strictly between the old top plane (y=1) and the new
    // cap plane (y=1.5) with a normal in the caps' plane pointing across the
    // old shared edge (x = 0): an internal wall would be exactly that.
    for (const BrushFace& face : out.Faces)
    {
        const Vec3d centroid = BrushFaceCentroid(out, face);
        const Vec3d normal = BrushComputeFaceNormal(out, face);
        const bool inBand = centroid.Y > 1.01f && centroid.Y < 1.49f;
        const bool crossesSeam = std::abs(centroid.X) < 1e-3f && std::abs(normal.X) > 0.9f;
        EXPECT_FALSE(inBand && crossesSeam);
    }
}

TEST(BrushOpsBevel, ProfileBulgesTowardTheOldCorner)
{
    // Rounded rows must stay on the outside of the flat chamfer plane, not
    // dip into the solid. For the +X/+Y edge of a unit box beveled by w, the
    // chamfer chord satisfies x + y = 2 - w; every profile vertex must sit at
    // or outside it.
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    std::array<std::uint32_t, 2> edge = { 0, 0 };
    bool found = false;
    for (std::uint32_t a = 0; a < box.Vertices.size() && !found; ++a)
        for (std::uint32_t b = a + 1; b < box.Vertices.size() && !found; ++b)
        {
            const Vec3d& pa = box.Vertices[a].Position;
            const Vec3d& pb = box.Vertices[b].Position;
            if (pa.X > 0.9f && pa.Y > 0.9f && pb.X > 0.9f && pb.Y > 0.9f
                && (pa - pb).SqrMagnitude() > 1.0f)
            {
                edge = { a, b };
                found = true;
            }
        }
    ASSERT_TRUE(found);

    constexpr float w = 0.4f;
    const std::array<std::uint32_t, 2> edges[] = { edge };
    const BrushMesh out = BrushOps::BevelEdges(box, edges, w, 4);
    ASSERT_GT(out.Vertices.size(), box.Vertices.size());
    for (const BrushVertex& v : out.Vertices)
        if (v.Position.X > 1.0f - w - 1e-4f && v.Position.Y > 1.0f - w - 1e-4f)
        {
            EXPECT_GE(v.Position.X + v.Position.Y, 2.0f - w - 1e-4f);
        }
}
