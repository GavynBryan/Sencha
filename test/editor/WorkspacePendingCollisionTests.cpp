#include "WorkspaceFixture.h"

// Several mechanisms stage previews through the command stack's single
// pending-edit scope, and any of them can begin while another is pending. These
// cover the handoff: the incumbent reverts its staged state, the claimant owns
// the scope, and exactly one preview survives.
namespace
{

class WorkspacePendingCollisionTest : public WorkspaceTest
{
protected:
    // Two separated boxes with one matching edge selected on each: the shape
    // BridgeSelectedEdges stages a cross-brush preview entity for.
    void SelectMatchingEdgesOnTwoBrushes()
    {
        First = AddBrush(Vec3d{ 0, 0, 0 });
        Second = AddBrush(Vec3d{ 4, 0, 0 });
        const std::vector<SelectableRef> firstEdges = RefsOf(First, MeshElementKind::Edge);
        const std::vector<SelectableRef> secondEdges = RefsOf(Second, MeshElementKind::Edge);
        ASSERT_FALSE(firstEdges.empty());
        ASSERT_FALSE(secondEdges.empty());
        Select({ firstEdges[0], secondEdges[0] });
    }

    [[nodiscard]] std::size_t FaceCount(EntityId entity)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(entity);
        return mesh == nullptr ? 0 : mesh->Faces.size();
    }

    EntityId First = {};
    EntityId Second = {};
};

TEST_F(WorkspacePendingCollisionTest, BeginningAnInsetDropsAPendingBridgePreview)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();
    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());
    ASSERT_EQ(BrushCount(), before + 1);

    // Switching to a face selection and insetting is reachable from the panel
    // while the bridge preview is live.
    SelectElements(First, MeshElementKind::Face, { 0 });
    Workspace.BeginInsetOnSelectedFaces(0.1f);

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_TRUE(Workspace.HasPendingInset());
    EXPECT_TRUE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before); // the bridge preview entity is gone
}

TEST_F(WorkspacePendingCollisionTest, BeginningABridgeRestoresAPendingInsetMesh)
{
    First = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(First, MeshElementKind::Face, { 0 });
    const std::size_t original = FaceCount(First);
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());
    ASSERT_GT(FaceCount(First), original);

    SelectMatchingEdgesOnTwoBrushes();
    Workspace.Actions.Bridge(1);

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_TRUE(Workspace.HasPendingBridge());
    EXPECT_TRUE(Commands.HasPendingEdit());
    EXPECT_EQ(FaceCount(First), original); // the previewed inset was reverted
}

TEST_F(WorkspacePendingCollisionTest, BevelOverAnInsetKeepsOneScopeAndRevertsTheInset)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    const std::size_t original = FaceCount(brush);
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());

    SelectElements(brush, MeshElementKind::Edge, { 0 });
    Workspace.BeginBevelOnSelectedEdges(0.05f, 2);

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_TRUE(Workspace.HasPendingBevel());
    EXPECT_TRUE(Commands.HasPendingEdit());

    // Cancelling the bevel returns the mesh all the way to its pre-inset shape:
    // the inset never reached the document.
    Workspace.CancelPendingElementEdit();
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(FaceCount(brush), original);
}

TEST_F(WorkspacePendingCollisionTest, UnloadingAZoneCancelsAPendingEditInsteadOfStrandingIt)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());
    ASSERT_TRUE(Workspace.World.IsZoneOpen(second));

    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    const std::size_t original = FaceCount(brush);
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());

    // The unloaded zone is not the focus zone, so the previewed document
    // survives the unload; its preview must not survive with it.
    ASSERT_TRUE(Workspace.World.UnloadZone(second));

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(FaceCount(brush), original);
}

TEST_F(WorkspacePendingCollisionTest, UnloadingAZoneDestroysAPendingBridgePreview)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());

    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();
    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());
    ASSERT_EQ(BrushCount(), before + 1);

    ASSERT_TRUE(Workspace.World.UnloadZone(second));

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before);
}

}
