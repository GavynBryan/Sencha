#include "meshedit/MeshElements.h"

#include "level/brush/BrushOps.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <vector>

TEST(MeshElements, ProjectsBoxFaces)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    Transform3f transform = Transform3f::Identity();
    transform.Position = { 10.0f, 0.0f, 0.0f };

    const std::vector<FaceElement> faces = MeshElements::Faces(box, transform);
    ASSERT_EQ(faces.size(), 6u);

    const auto plusX = std::find_if(faces.begin(), faces.end(), [](const FaceElement& face)
    {
        return face.Normal.X > 0.9f;
    });
    ASSERT_NE(plusX, faces.end());
    EXPECT_FLOAT_EQ(plusX->Center.X, 11.0f);
    EXPECT_EQ(plusX->Corners.size(), 4u);
}

TEST(MeshElements, ProjectsBoxVertices)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    Transform3f transform = Transform3f::Identity();
    transform.Position = { 0.0f, 2.0f, 0.0f };

    const std::vector<VertexElement> vertices = MeshElements::Vertices(box, transform);
    ASSERT_EQ(vertices.size(), 8u);
    EXPECT_FLOAT_EQ(vertices[0].Position.Y, 1.0f);
}

TEST(MeshElements, ProjectsUniqueDeterministicBoxEdges)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const Transform3f transform = Transform3f::Identity();

    const std::vector<EdgeElement> edges = MeshElements::Edges(box, transform);
    ASSERT_EQ(edges.size(), 12u);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    pairs.reserve(edges.size());
    for (const EdgeElement& edge : edges)
        pairs.emplace_back(edge.VertexA, edge.VertexB);

    EXPECT_TRUE(std::is_sorted(pairs.begin(), pairs.end()));
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    EXPECT_EQ(pairs.size(), 12u);
}

TEST(MeshElements, LookupRejectsStaleIndices)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const Transform3f transform = Transform3f::Identity();

    EXPECT_FALSE(MeshElements::TryGetFace(box, transform, 99).has_value());
    EXPECT_FALSE(MeshElements::TryGetEdge(box, transform, 99).has_value());
    EXPECT_FALSE(MeshElements::TryGetVertex(box, transform, 99).has_value());
}
