#include "brush/BrushHalfEdge.h"
#include "brush/BrushOps.h"

#include <gtest/gtest.h>

TEST(BrushHalfEdge, ClosedMeshHasAllTwins)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushHalfEdgeMesh he = BrushBuildHalfEdge(box);

    EXPECT_EQ(he.FaceToHalfEdge.size(), box.Faces.size());
    EXPECT_EQ(he.HalfEdges.size(), 6u * 4u); // 6 quads × 4 edges
    for (const BrushHalfEdge& edge : he.HalfEdges)
        EXPECT_NE(edge.Twin, BrushInvalidIndex); // a closed solid has no boundary edges
}

TEST(BrushHalfEdge, OpenMeshHasBoundaryEdges)
{
    const BrushMesh opened = BrushOps::DeleteFace(BrushOps::MakeBox({ 1, 1, 1 }), 0);
    const BrushHalfEdgeMesh he = BrushBuildHalfEdge(opened);

    int boundary = 0;
    for (const BrushHalfEdge& edge : he.HalfEdges)
        if (edge.Twin == BrushInvalidIndex)
            ++boundary;
    EXPECT_GT(boundary, 0); // the deleted face left a boundary loop
}

TEST(BrushHalfEdge, RoundTripPreservesTopology)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushMesh rebuilt = BrushToFaceVertex(BrushBuildHalfEdge(box));

    ASSERT_EQ(rebuilt.Vertices.size(), box.Vertices.size());
    ASSERT_EQ(rebuilt.Faces.size(), box.Faces.size());
    for (std::size_t i = 0; i < box.Faces.size(); ++i)
        EXPECT_EQ(rebuilt.Faces[i].Loop, box.Faces[i].Loop);
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
    {
        EXPECT_FLOAT_EQ(rebuilt.Vertices[i].Position.X, box.Vertices[i].Position.X);
        EXPECT_FLOAT_EQ(rebuilt.Vertices[i].Position.Y, box.Vertices[i].Position.Y);
        EXPECT_FLOAT_EQ(rebuilt.Vertices[i].Position.Z, box.Vertices[i].Position.Z);
    }
}
