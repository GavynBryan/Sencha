#include "WorkspaceFixture.h"

// The cross-brush bridge state machine. A bridge between two brushes stages a
// live preview entity that the segment count regenerates, and that commit or
// cancel must dispose of exactly once. Bridge geometry is covered by
// BrushOpsTests; same-brush bridging is covered by MeshEditServiceTests.
namespace
{

class WorkspacePendingBridgeTest : public WorkspaceTest
{
protected:
    // Two separated boxes with one matching edge selected on each: the shape
    // BridgeSelectedEdges treats as a cross-brush bridge.
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

    EntityId First = {};
    EntityId Second = {};
};

TEST_F(WorkspacePendingBridgeTest, CrossBrushBridgeStagesAPreviewEntity)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.Actions.Bridge(1);

    EXPECT_TRUE(Workspace.HasPendingBridge());
    EXPECT_TRUE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before + 1);
}

TEST_F(WorkspacePendingBridgeTest, CancelDestroysThePreviewEntity)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());
    Workspace.CancelPendingBridge();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspacePendingBridgeTest, CommitKeepsTheBridgeAsOneUndoStep)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.Actions.Bridge(1);
    Workspace.CommitPendingBridge();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before + 1);
    ASSERT_TRUE(Commands.CanUndo());

    Commands.Undo();
    EXPECT_EQ(BrushCount(), before);
}

TEST_F(WorkspacePendingBridgeTest, UndoWhilePendingDropsThePreview)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.Actions.Bridge(1);
    Commands.Undo();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_EQ(BrushCount(), before);
}

// The segment count regenerates the preview in place: the staged entity is
// reused rather than a second one being created.
TEST_F(WorkspacePendingBridgeTest, ChangingSegmentsRegeneratesWithoutStagingAnother)
{
    SelectMatchingEdgesOnTwoBrushes();

    Workspace.Actions.Bridge(1);
    const std::size_t staged = BrushCount();
    Workspace.SetPendingBridgeSegments(4);

    EXPECT_TRUE(Workspace.HasPendingBridge());
    EXPECT_EQ(BrushCount(), staged);
}

TEST_F(WorkspacePendingBridgeTest, SegmentChangeWithNothingPendingIsANoOp)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.SetPendingBridgeSegments(4);

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_EQ(BrushCount(), before);
}

// Re-bridging replaces the staged preview instead of leaking the first one.
TEST_F(WorkspacePendingBridgeTest, RebridgingReplacesThePreviousPreview)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.Actions.Bridge(1);
    const std::vector<SelectableRef> firstEdges = RefsOf(First, MeshElementKind::Edge);
    const std::vector<SelectableRef> secondEdges = RefsOf(Second, MeshElementKind::Edge);
    Select({ firstEdges[1], secondEdges[1] });
    Workspace.Actions.Bridge(1);

    EXPECT_TRUE(Workspace.HasPendingBridge());
    EXPECT_EQ(BrushCount(), before + 1);
}

// Paths of different lengths cannot be paired, so nothing stages.
TEST_F(WorkspacePendingBridgeTest, MismatchedPathLengthsStageNothing)
{
    const EntityId first = AddBrush(Vec3d{ 0, 0, 0 });
    const EntityId second = AddBrush(Vec3d{ 4, 0, 0 });
    const std::vector<SelectableRef> firstEdges = RefsOf(first, MeshElementKind::Edge);
    const std::vector<SelectableRef> secondEdges = RefsOf(second, MeshElementKind::Edge);
    ASSERT_GE(firstEdges.size(), 3u);
    Select({ firstEdges[0], firstEdges[1], firstEdges[2], secondEdges[0] });
    const std::size_t before = BrushCount();

    Workspace.Actions.Bridge(1);

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before);
}

// One brush's edges are a same-brush bridge: an immediate mesh edit, never a
// staged preview.
TEST_F(WorkspacePendingBridgeTest, SingleBrushSelectionNeverStagesAPreview)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    const std::vector<SelectableRef> edges = RefsOf(brush, MeshElementKind::Edge);
    ASSERT_GE(edges.size(), 2u);
    Select({ edges[0], edges[1] });

    Workspace.Actions.Bridge(1);

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspacePendingBridgeTest, CommitWithNothingPendingIsANoOp)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.CommitPendingBridge();

    EXPECT_EQ(BrushCount(), before);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspacePendingBridgeTest, CancelWithNothingPendingIsANoOp)
{
    SelectMatchingEdgesOnTwoBrushes();
    const std::size_t before = BrushCount();

    Workspace.CancelPendingBridge();

    EXPECT_EQ(BrushCount(), before);
    EXPECT_FALSE(Commands.HasPendingEdit());
}

// Losing the staged entity before commit must close the scope rather than
// committing a dead reference.
TEST_F(WorkspacePendingBridgeTest, CommitAfterThePreviewEntityDiesLeavesNoPendingState)
{
    SelectMatchingEdgesOnTwoBrushes();
    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());

    // The preview is the only brush that is neither source.
    for (const EntityId entity : std::vector<EntityId>(Scene().GetAllEntities().begin(),
                                                        Scene().GetAllEntities().end()))
    {
        if (!(entity == First) && !(entity == Second) && Scene().TryGetBrushMesh(entity) != nullptr)
            Scene().DestroyEntity(entity);
    }

    Workspace.CommitPendingBridge();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

} // namespace
