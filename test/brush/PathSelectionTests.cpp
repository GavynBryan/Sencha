#include "brush/BrushOps.h"
#include "meshedit/MeshElements.h"
#include "meshedit/PathSelection.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
const RegistryId kRegistry = RegistryId::Global();

EntityId TestEntity()
{
    return EntityId{ 7, 1 };
}

// The edge index of the sorted vertex pair (a, b) under the shared enumeration.
std::uint32_t EdgeIndexOf(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    for (const EdgeElement& edge : MeshElements::Edges(mesh, Transform3f::Identity()))
        if ((edge.VertexA == a && edge.VertexB == b) || (edge.VertexA == b && edge.VertexB == a))
            return edge.Index;
    ADD_FAILURE() << "edge (" << a << ", " << b << ") not found";
    return 0;
}
}

TEST(PathSelection, VertexPathCrossesTheBox)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    // Find two diagonally opposite corners: their offset is (2,2,2) in some sign
    // combination, so the shortest edge walk is 3 hops = 4 vertices.
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    for (std::uint32_t v = 1; v < box.Vertices.size(); ++v)
    {
        const Vec3d d = box.Vertices[v].Position - box.Vertices[0].Position;
        if (std::abs(d.X) > 1.5f && std::abs(d.Y) > 1.5f && std::abs(d.Z) > 1.5f)
            to = v;
    }
    ASSERT_NE(from, to);

    const auto path = GatherPathSelection(box,
        SelectableRef::VertexSelection(kRegistry, TestEntity(), from),
        SelectableRef::VertexSelection(kRegistry, TestEntity(), to));

    ASSERT_EQ(path.size(), 4u);
    EXPECT_EQ(path.front().ElementId, from);
    EXPECT_EQ(path.back().ElementId, to);
    for (const SelectableRef& ref : path)
        EXPECT_TRUE(ref.IsVertex());
}

TEST(PathSelection, FacePathCrossesOppositeFaces)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    // Opposite faces of a box share no edge: shortest path is 3 faces.
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    const Vec3d n0 = box.Faces[0].Normal;
    for (std::uint32_t f = 1; f < box.Faces.size(); ++f)
        if (box.Faces[f].Normal.Dot(n0) < -0.9f)
            to = f;
    ASSERT_NE(from, to);

    const auto path = GatherPathSelection(box,
        SelectableRef::FaceSelection(kRegistry, TestEntity(), from),
        SelectableRef::FaceSelection(kRegistry, TestEntity(), to));

    ASSERT_EQ(path.size(), 3u);
    EXPECT_EQ(path.front().ElementId, from);
    EXPECT_EQ(path.back().ElementId, to);
}

TEST(PathSelection, EdgePathStepsAcrossSharedVertices)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const auto edges = MeshElements::Edges(box, Transform3f::Identity());
    ASSERT_EQ(edges.size(), 12u);

    // Two edges sharing a vertex: path is exactly the two of them.
    std::uint32_t from = edges[0].Index;
    std::uint32_t to = 0;
    bool found = false;
    for (const EdgeElement& edge : edges)
    {
        if (edge.Index == from)
            continue;
        if (edge.VertexA == edges[0].VertexA || edge.VertexB == edges[0].VertexA)
        {
            to = edge.Index;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    const auto path = GatherPathSelection(box,
        SelectableRef::EdgeSelection(kRegistry, TestEntity(), from),
        SelectableRef::EdgeSelection(kRegistry, TestEntity(), to));
    ASSERT_EQ(path.size(), 2u);
    EXPECT_EQ(path.front().ElementId, from);
    EXPECT_EQ(path.back().ElementId, to);
}

TEST(PathSelection, MismatchedKindOrEntityYieldsEmpty)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    EXPECT_TRUE(GatherPathSelection(box,
        SelectableRef::VertexSelection(kRegistry, TestEntity(), 0),
        SelectableRef::EdgeSelection(kRegistry, TestEntity(), 1)).empty());

    EXPECT_TRUE(GatherPathSelection(box,
        SelectableRef::VertexSelection(kRegistry, TestEntity(), 0),
        SelectableRef::VertexSelection(kRegistry, EntityId{ 9, 1 }, 1)).empty());
}
