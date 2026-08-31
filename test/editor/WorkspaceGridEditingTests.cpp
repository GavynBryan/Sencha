#include "WorkspaceFixture.h"

#include "viewport/GridFrame.h"

#include <cmath>

// The workspace grid verbs: what a selection resolves the frame to, and what
// leaves it untouched. The frame math itself is covered by GridFrameTests.
namespace
{

class WorkspaceGridEditingTest : public WorkspaceTest
{
protected:
    [[nodiscard]] static bool NearlyEqual(Vec3d a, Vec3d b)
    {
        return (a - b).Magnitude() < 1e-4;
    }
};

TEST_F(WorkspaceGridEditingTest, OriginFollowsASingleSelectedVertex)
{
    const EntityId brush = AddBrush(Vec3d{ 4, 0, 0 });
    const std::vector<SelectableRef> vertices = RefsOf(brush, MeshElementKind::Vertex);
    ASSERT_FALSE(vertices.empty());

    const auto* mesh = Scene().TryGetBrushMesh(brush);
    const auto* transform = Scene().TryGetWorldTransform(brush);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(transform, nullptr);
    const auto expected = MeshElements::TryGetVertex(*mesh, *transform, vertices[0].ElementId);
    ASSERT_TRUE(expected.has_value());

    Select({ vertices[0] });
    Workspace.SetGridOriginToSelection();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.Origin, expected->Position));
}

// Two vertices are ambiguous as an exact anchor, so the rule falls through to
// the selection's bounds center rather than picking one of them.
TEST_F(WorkspaceGridEditingTest, TwoSelectedVerticesFallBackToBoundsCenter)
{
    const EntityId brush = AddBrush(Vec3d{ 4, 0, 0 });
    const std::vector<SelectableRef> vertices = RefsOf(brush, MeshElementKind::Vertex);
    ASSERT_GE(vertices.size(), 2u);

    Select({ vertices[0], vertices[1] });
    Workspace.SetGridOriginToSelection();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.Origin, Vec3d{ 4, 0, 0 }));
}

TEST_F(WorkspaceGridEditingTest, OriginFollowsTheEntityBoundsCenter)
{
    const EntityId brush = AddBrush(Vec3d{ -3, 2, 7 });
    SelectEntity(brush);

    Workspace.SetGridOriginToSelection();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.Origin, Vec3d{ -3, 2, 7 }));
}

TEST_F(WorkspaceGridEditingTest, EmptySelectionLeavesTheOriginAlone)
{
    Workspace.Grid.Origin = Vec3d{ 9, 9, 9 };
    Select({});

    Workspace.SetGridOriginToSelection();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.Origin, Vec3d{ 9, 9, 9 }));
}

TEST_F(WorkspaceGridEditingTest, AligningToAFacePutsTheFrameOnThatFacePlane)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    const std::vector<SelectableRef> faces = RefsOf(brush, MeshElementKind::Face);
    ASSERT_FALSE(faces.empty());

    const auto* mesh = Scene().TryGetBrushMesh(brush);
    const auto* transform = Scene().TryGetWorldTransform(brush);
    const auto face = MeshElements::TryGetFace(*mesh, *transform, faces[0].ElementId);
    ASSERT_TRUE(face.has_value());

    Select({ faces[0] });
    Workspace.AlignGridToSelectedFace();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.Origin, face->Center));
    Vec3d u;
    Vec3d n;
    Vec3d v;
    GridFrame::Basis(Workspace.Grid, u, n, v);
    EXPECT_NEAR(std::abs(n.Dot(face->Normal)), 1.0, 1e-4);
    // U is derived by projecting an edge of the face into the face plane, so it
    // carries no component along the normal.
    EXPECT_NEAR(u.Dot(face->Normal), 0.0, 1e-4);
}

// The frame normal lands anti-parallel to the face normal: FromFace stores
// AxisV = normal x U, and Basis re-derives the normal as AxisV x AxisU, which
// evaluates to -normal. The lattice is the same plane either way; the sign is
// what SyncOrthoViewsToGridFrame maps the ortho view axes through. Recorded so
// a deliberate change to the convention shows up here.
TEST_F(WorkspaceGridEditingTest, FaceAlignedFrameNormalOpposesTheFaceNormal)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    const std::vector<SelectableRef> faces = RefsOf(brush, MeshElementKind::Face);
    ASSERT_FALSE(faces.empty());

    const auto* mesh = Scene().TryGetBrushMesh(brush);
    const auto* transform = Scene().TryGetWorldTransform(brush);
    const auto face = MeshElements::TryGetFace(*mesh, *transform, faces[0].ElementId);
    ASSERT_TRUE(face.has_value());

    Select({ faces[0] });
    Workspace.AlignGridToSelectedFace();

    Vec3d u;
    Vec3d n;
    Vec3d v;
    GridFrame::Basis(Workspace.Grid, u, n, v);
    EXPECT_NEAR(n.Dot(face->Normal), -1.0, 1e-4);
}

TEST_F(WorkspaceGridEditingTest, AligningWithNoFaceSelectedLeavesTheFrameAlone)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectEntity(brush);
    const Vec3d axisU = Workspace.Grid.AxisU;
    const Vec3d origin = Workspace.Grid.Origin;

    Workspace.AlignGridToSelectedFace();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.AxisU, axisU));
    EXPECT_TRUE(NearlyEqual(Workspace.Grid.Origin, origin));
}

// Reset returns the axes to world alignment. It is a frame verb, so a moved
// origin is not part of what it restores.
TEST_F(WorkspaceGridEditingTest, ResetRestoresTheWorldAlignedAxes)
{
    Workspace.RotateGridInPlane(30.0f);
    ASSERT_FALSE(NearlyEqual(Workspace.Grid.AxisU, Vec3d{ 1, 0, 0 }));

    Workspace.ResetGrid();

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.AxisU, Vec3d{ 1, 0, 0 }));
    EXPECT_TRUE(NearlyEqual(Workspace.Grid.AxisV, Vec3d{ 0, 0, 1 }));
}

TEST_F(WorkspaceGridEditingTest, FourQuarterTurnsReturnTheFrameToWhereItStarted)
{
    const Vec3d axisU = Workspace.Grid.AxisU;

    for (int turn = 0; turn < 4; ++turn)
        Workspace.RotateGridInPlane(90.0f);

    EXPECT_TRUE(NearlyEqual(Workspace.Grid.AxisU, axisU));
}

// None of the frame verbs are document edits, so none of them may land on the
// undo stack.
TEST_F(WorkspaceGridEditingTest, GridVerbsAreNotUndoSteps)
{
    const EntityId brush = AddBrush(Vec3d{ 1, 1, 1 });
    SelectEntity(brush);

    Workspace.SetGridOriginToSelection();
    Workspace.RotateGridInPlane(15.0f);
    Workspace.ResetGrid();

    EXPECT_FALSE(Commands.CanUndo());
}

} // namespace
