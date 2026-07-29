#include "WorkspaceFixture.h"

// ResetInteractionState is the single path both document open and focus change
// go through. Everything bound to the outgoing document must be torn down, and
// the editing stack must come back usable.
namespace
{

class WorkspaceInteractionResetTest : public WorkspaceTest
{
};

TEST_F(WorkspaceInteractionResetTest, ResetCancelsAPendingElementEdit)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    const BrushMesh* mesh = Scene().TryGetBrushMesh(brush);
    ASSERT_NE(mesh, nullptr);
    const std::size_t before = mesh->Faces.size();
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());

    Workspace.ResetInteractionState();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Faces.size(), before);
}

TEST_F(WorkspaceInteractionResetTest, ResetCancelsAPendingBridgeAndDropsItsPreview)
{
    const EntityId first = AddBrush(Vec3d{ 0, 0, 0 });
    const EntityId second = AddBrush(Vec3d{ 4, 0, 0 });
    Select({ RefsOf(first, MeshElementKind::Edge)[0], RefsOf(second, MeshElementKind::Edge)[0] });
    const std::size_t before = BrushCount();
    Workspace.BridgeSelectedEdges(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());

    Workspace.ResetInteractionState();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before);
}

TEST_F(WorkspaceInteractionResetTest, ResetClearsSelectionAndUndoHistory)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.ActiveMaterial.Active = AssetRef{ AssetType::Material, "materials/test.smat" };
    Workspace.ApplyActiveMaterialToSelectedFaces();
    ASSERT_TRUE(Commands.CanUndo());

    Workspace.ResetInteractionState();

    EXPECT_TRUE(Workspace.Selection.GetSelection().empty());
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspaceInteractionResetTest, ResetClearsTransientOverlayState)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectEntity(brush);
    Workspace.UpdateOverlay();
    ASSERT_FALSE(Workspace.Overlay.Labels.empty());

    Workspace.ResetInteractionState();

    EXPECT_TRUE(Workspace.Overlay.Labels.empty());
}

// The rebuilt stack has to be usable, not merely non-null: a pending edit
// begun after a reset must stage and roll back like any other.
TEST_F(WorkspaceInteractionResetTest, TheEditingStackWorksAgainAfterAReset)
{
    Workspace.ResetInteractionState();

    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    const std::size_t before = Scene().TryGetBrushMesh(brush)->Faces.size();
    SelectElements(brush, MeshElementKind::Face, { 0 });

    Workspace.BeginInsetOnSelectedFaces(0.1f);
    EXPECT_TRUE(Workspace.HasPendingInset());
    EXPECT_GT(Scene().TryGetBrushMesh(brush)->Faces.size(), before);

    Workspace.CancelPendingElementEdit();
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Faces.size(), before);
}

TEST_F(WorkspaceInteractionResetTest, RepeatWithNothingRecordedIsANoOp)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectEntity(brush);
    const std::size_t before = BrushCount();

    Workspace.RepeatLastAction();

    EXPECT_EQ(BrushCount(), before);
    EXPECT_FALSE(Commands.CanUndo());
}

} // namespace
