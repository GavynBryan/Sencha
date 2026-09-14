#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
constexpr float kTol = 1e-4f;

// How many faces use the undirected edge (a, b) as a consecutive pair.
int LoopsUsingEdge(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    int count = 0;
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t from = face.Loop[i];
            const std::uint32_t to = face.Loop[(i + 1) % face.Loop.size()];
            if ((from == a && to == b) || (from == b && to == a))
                ++count;
        }
    return count;
}

// An edge of the box that two faces share.
std::pair<std::uint32_t, std::uint32_t> SharedEdge(const BrushMesh& mesh)
{
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % face.Loop.size()];
            if (LoopsUsingEdge(mesh, a, b) == 2)
                return { a, b };
        }
    return { 0, 0 };
}
}

TEST(InsertVertexOnEdge, SplitsEveryIncidentLoopAtOnce)
{
    // A T-junction is not a thing to be repaired afterwards: an operation that
    // needs a vertex on an edge has to put it in both faces in one step, or the
    // mesh is momentarily not a manifold and validation has to guess.
    const BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const auto [a, b] = SharedEdge(box);
    const Vec3d midpoint = (box.Vertices[a].Position + box.Vertices[b].Position) * 0.5f;

    const auto split = BrushOps::InsertVertexOnEdge(box, a, b, midpoint, kTol);
    ASSERT_TRUE(split.has_value());

    EXPECT_EQ(split->Mesh.Vertices.size(), box.Vertices.size() + 1);
    EXPECT_EQ(split->Mesh.Faces.size(), box.Faces.size()) << "splitting an edge adds no faces";
    EXPECT_EQ(LoopsUsingEdge(split->Mesh, a, b), 0) << "the original edge survived somewhere";
    EXPECT_EQ(LoopsUsingEdge(split->Mesh, a, split->Vertex), 2);
    EXPECT_EQ(LoopsUsingEdge(split->Mesh, split->Vertex, b), 2);
    EXPECT_NEAR((split->Mesh.Vertices[split->Vertex].Position - midpoint).Magnitude(), 0.0f, 1e-6f);

    BrushMesh validated = split->Mesh;
    const BrushRepairResult report = BrushValidateAndRepair(validated);
    EXPECT_TRUE(report.Ok);
    EXPECT_TRUE(report.Closed) << "the split opened the solid";
    EXPECT_FALSE(report.Changed) << "repair had to clean up after the split";
}

TEST(InsertVertexOnEdge, ASoftEdgeStaysSoftOnBothHalves)
{
    // Shading is the visible consequence: half a smooth edge going hard puts a
    // seam across a surface the user marked smooth.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const auto [a, b] = SharedEdge(box);
    BrushSetEdgeSoft(box, a, b, true);
    const Vec3d midpoint = (box.Vertices[a].Position + box.Vertices[b].Position) * 0.5f;

    const auto split = BrushOps::InsertVertexOnEdge(box, a, b, midpoint, kTol);
    ASSERT_TRUE(split.has_value());

    EXPECT_TRUE(BrushEdgeIsSoft(split->Mesh, a, split->Vertex));
    EXPECT_TRUE(BrushEdgeIsSoft(split->Mesh, split->Vertex, b));
    EXPECT_FALSE(BrushEdgeIsSoft(split->Mesh, a, b)) << "the replaced edge is still marked";
}

TEST(InsertVertexOnEdge, AHardEdgeStaysHard)
{
    const BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const auto [a, b] = SharedEdge(box);
    const Vec3d midpoint = (box.Vertices[a].Position + box.Vertices[b].Position) * 0.5f;

    const auto split = BrushOps::InsertVertexOnEdge(box, a, b, midpoint, kTol);
    ASSERT_TRUE(split.has_value());
    EXPECT_TRUE(split->Mesh.SoftEdges.empty());
}

TEST(InsertVertexOnEdge, RefusesAPointThatIsNotOnTheEdge)
{
    const BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const auto [a, b] = SharedEdge(box);
    Vec3d offEdge = (box.Vertices[a].Position + box.Vertices[b].Position) * 0.5f;
    offEdge.X += 0.3f;
    offEdge.Y += 0.3f;
    offEdge.Z += 0.3f;

    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, a, b, offEdge, kTol).has_value());
}

TEST(InsertVertexOnEdge, RefusesASplitThatWouldWeldAway)
{
    // A vertex placed a hair from an endpoint leaves an edge shorter than the
    // weld tolerance, so validation would immediately merge the two and the
    // caller would be holding an index to a vertex that no longer exists.
    const BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const auto [a, b] = SharedEdge(box);
    const Vec3d start = box.Vertices[a].Position;
    const Vec3d edge = box.Vertices[b].Position - start;

    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, a, b, start, kTol).has_value());
    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, a, b, start + edge * 1e-6f, kTol).has_value());
    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, a, b, start + edge, kTol).has_value());
}

TEST(InsertVertexOnEdge, RefusesAnEdgeTheMeshDoesNotHave)
{
    const BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    // Two vertices of the box diagonally opposite: a segment, but not an edge.
    std::uint32_t far = 0;
    for (std::uint32_t i = 1; i < box.Vertices.size(); ++i)
        if ((box.Vertices[i].Position - box.Vertices[0].Position).SqrMagnitude()
            > (box.Vertices[far].Position - box.Vertices[0].Position).SqrMagnitude())
            far = i;
    const Vec3d midpoint = (box.Vertices[0].Position + box.Vertices[far].Position) * 0.5f;

    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, 0, far, midpoint, kTol).has_value());
    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, 0, 0, midpoint, kTol).has_value());
    EXPECT_FALSE(BrushOps::InsertVertexOnEdge(box, 0, 9999, midpoint, kTol).has_value());
}
