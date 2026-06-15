#include "level/brush/BrushOps.h"
#include "level/brush/BrushValidation.h"

#include <gtest/gtest.h>

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
