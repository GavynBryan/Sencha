#include "WorkspaceFixture.h"

// The pending inset/bevel state machine: the preview it opens, the parameter
// updates it regenerates from, and the two exits (commit, cancel). The inset
// and bevel geometry itself is covered by BrushOpsTests.
namespace
{

class WorkspacePendingElementEditTest : public WorkspaceTest
{
protected:
    // A box brush with its first face selected, ready to inset.
    EntityId BrushWithFaceSelected()
    {
        const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
        SelectElements(brush, MeshElementKind::Face, { 0 });
        return brush;
    }

    // A box brush with its first edge selected, ready to bevel.
    EntityId BrushWithEdgeSelected()
    {
        const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
        SelectElements(brush, MeshElementKind::Edge, { 0 });
        return brush;
    }

    [[nodiscard]] std::size_t FaceCount(EntityId entity)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(entity);
        return mesh == nullptr ? 0 : mesh->Faces.size();
    }
};

TEST_F(WorkspacePendingElementEditTest, InsetBeginsPendingAndPreviewsMoreFaces)
{
    const EntityId brush = BrushWithFaceSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.BeginInsetOnSelectedFaces(0.1f);

    EXPECT_TRUE(Workspace.HasPendingInset());
    EXPECT_FALSE(Workspace.HasPendingBevel());
    EXPECT_TRUE(Commands.HasPendingEdit());
    EXPECT_GT(FaceCount(brush), before);
}

TEST_F(WorkspacePendingElementEditTest, InsetWithNoFaceSelectedStaysIdle)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectEntity(brush);

    Workspace.BeginInsetOnSelectedFaces(0.1f);

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspacePendingElementEditTest, CancelRestoresTheOriginalMeshAndClosesTheScope)
{
    const EntityId brush = BrushWithFaceSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_GT(FaceCount(brush), before);
    Workspace.CancelPendingElementEdit();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(FaceCount(brush), before);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspacePendingElementEditTest, CommitKeepsTheEditAsOneUndoStep)
{
    const EntityId brush = BrushWithFaceSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.BeginInsetOnSelectedFaces(0.1f);
    Workspace.CommitPendingElementEdit();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_GT(FaceCount(brush), before);
    ASSERT_TRUE(Commands.CanUndo());

    Commands.Undo();
    EXPECT_EQ(FaceCount(brush), before);
}

// Undo while pending is the cancel path: it rolls the preview back instead of
// reaching the committed history.
TEST_F(WorkspacePendingElementEditTest, UndoWhilePendingCancelsThePreview)
{
    const EntityId brush = BrushWithFaceSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.BeginInsetOnSelectedFaces(0.1f);
    Commands.Undo();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_EQ(FaceCount(brush), before);
}

TEST_F(WorkspacePendingElementEditTest, DistanceUpdateRegeneratesFromTheOriginal)
{
    const EntityId brush = BrushWithFaceSelected();

    Workspace.BeginInsetOnSelectedFaces(0.1f);
    const std::size_t afterFirst = FaceCount(brush);
    Workspace.SetPendingInsetDistance(0.3f);

    // Regeneration replays against the captured original, so the topology does
    // not accumulate with each parameter change.
    EXPECT_EQ(FaceCount(brush), afterFirst);
    EXPECT_TRUE(Workspace.HasPendingInset());
}

TEST_F(WorkspacePendingElementEditTest, DistanceUpdateWithNothingPendingIsANoOp)
{
    const EntityId brush = BrushWithFaceSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.SetPendingInsetDistance(0.5f);

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_EQ(FaceCount(brush), before);
}

TEST_F(WorkspacePendingElementEditTest, BevelBeginsPendingOnSelectedEdges)
{
    const EntityId brush = BrushWithEdgeSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.BeginBevelOnSelectedEdges(0.1f, 1);

    EXPECT_TRUE(Workspace.HasPendingBevel());
    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_TRUE(Commands.HasPendingEdit());
    EXPECT_GT(FaceCount(brush), before);
}

TEST_F(WorkspacePendingElementEditTest, BevelCancelRestoresTheOriginalMesh)
{
    const EntityId brush = BrushWithEdgeSelected();
    const std::size_t before = FaceCount(brush);

    Workspace.BeginBevelOnSelectedEdges(0.1f, 2);
    Workspace.CancelPendingElementEdit();

    EXPECT_FALSE(Workspace.HasPendingBevel());
    EXPECT_EQ(FaceCount(brush), before);
}

// The beveled edges are gone after a commit, so their refs must not survive as
// a stale element selection.
TEST_F(WorkspacePendingElementEditTest, CommittingABevelClearsTheElementSelection)
{
    BrushWithEdgeSelected();
    ASSERT_FALSE(Workspace.Selection.GetSelection().empty());

    Workspace.BeginBevelOnSelectedEdges(0.1f, 1);
    Workspace.CommitPendingElementEdit();

    for (const SelectableRef& ref : Workspace.Selection.GetSelection())
        EXPECT_FALSE(ref.IsEdge());
}

// One machine holds both operations, so starting the second replaces the first
// rather than leaving two previews live.
TEST_F(WorkspacePendingElementEditTest, StartingABevelReplacesAPendingInset)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());

    SelectElements(brush, MeshElementKind::Edge, { 0 });
    Workspace.BeginBevelOnSelectedEdges(0.1f, 1);

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_TRUE(Workspace.HasPendingBevel());
    EXPECT_TRUE(Commands.HasPendingEdit());
}

TEST_F(WorkspacePendingElementEditTest, CommitWithNothingPendingIsANoOp)
{
    (void)AddBrush(Vec3d{ 0, 0, 0 });

    Workspace.CommitPendingElementEdit();

    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspacePendingElementEditTest, CancelWithNothingPendingIsANoOp)
{
    (void)AddBrush(Vec3d{ 0, 0, 0 });

    Workspace.CancelPendingElementEdit();

    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

// The pending edit references an entity; losing it must not leave the scope
// open or the machine mid-edit.
TEST_F(WorkspacePendingElementEditTest, CommitAfterTheEditedEntityDiesLeavesNoPendingState)
{
    const EntityId brush = BrushWithFaceSelected();
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());

    Scene().DestroyEntity(brush);
    Workspace.CommitPendingElementEdit();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspacePendingElementEditTest, CancelAfterTheEditedEntityDiesLeavesNoPendingState)
{
    const EntityId brush = BrushWithFaceSelected();
    Workspace.BeginInsetOnSelectedFaces(0.1f);

    Scene().DestroyEntity(brush);
    Workspace.CancelPendingElementEdit();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

} // namespace
