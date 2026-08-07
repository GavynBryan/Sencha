#include "WorkspaceFixture.h"

#include <filesystem>

// Persisting the document settles whatever is staged first, so no preview ever
// reaches a file or a cook half-applied. Resolution is by commit: the geometry
// on screen is the geometry that gets written, and each settlement is its own
// undo step.
namespace
{

class WorkspaceResolvePendingEditsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Dir = std::filesystem::temp_directory_path() / "sencha_workspace_resolve_tests";
        std::filesystem::remove_all(Dir);
        std::filesystem::create_directories(Dir);
    }

    void TearDown() override { std::filesystem::remove_all(Dir); }

    [[nodiscard]] std::size_t FaceCount(EntityId entity)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(entity);
        return mesh == nullptr ? 0 : mesh->Faces.size();
    }

    std::filesystem::path Dir;
};

TEST_F(WorkspaceResolvePendingEditsTest, ResolvingCommitsAPendingInsetAsOneUndoStep)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    const std::size_t original = FaceCount(brush);
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    const std::size_t previewed = FaceCount(brush);
    ASSERT_GT(previewed, original);

    Workspace.ResolvePendingEdits();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(FaceCount(brush), previewed); // the previewed geometry is what stuck
    ASSERT_TRUE(Commands.CanUndo());

    Commands.Undo();
    EXPECT_EQ(FaceCount(brush), original);
}

TEST_F(WorkspaceResolvePendingEditsTest, ResolvingCommitsAPendingBridgeAsOneUndoStep)
{
    const EntityId first = AddBrush(Vec3d{ 0, 0, 0 });
    const EntityId second = AddBrush(Vec3d{ 4, 0, 0 });
    const std::vector<SelectableRef> firstEdges = RefsOf(first, MeshElementKind::Edge);
    const std::vector<SelectableRef> secondEdges = RefsOf(second, MeshElementKind::Edge);
    ASSERT_FALSE(firstEdges.empty());
    ASSERT_FALSE(secondEdges.empty());
    Select({ firstEdges[0], secondEdges[0] });

    const std::size_t before = BrushCount();
    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());

    Workspace.ResolvePendingEdits();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(BrushCount(), before + 1);
    ASSERT_TRUE(Commands.CanUndo());

    Commands.Undo();
    EXPECT_EQ(BrushCount(), before);
}

TEST_F(WorkspaceResolvePendingEditsTest, ResolvingWithNothingStagedIsANoOp)
{
    (void)AddBrush(Vec3d{ 0, 0, 0 });

    Workspace.ResolvePendingEdits();

    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspaceResolvePendingEditsTest, SavedWorldContainsTheResolvedBridge)
{
    const EntityId first = AddBrush(Vec3d{ 0, 0, 0 });
    const EntityId second = AddBrush(Vec3d{ 4, 0, 0 });
    const std::vector<SelectableRef> firstEdges = RefsOf(first, MeshElementKind::Edge);
    const std::vector<SelectableRef> secondEdges = RefsOf(second, MeshElementKind::Edge);
    ASSERT_FALSE(firstEdges.empty());
    ASSERT_FALSE(secondEdges.empty());
    Select({ firstEdges[0], secondEdges[0] });

    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());
    const std::size_t staged = BrushCount();

    // The save path resolves first, so the bridge is real geometry by the time
    // the file is written; reopening it must find the same brush count.
    Workspace.ResolvePendingEdits();
    const std::string worldPath = (Dir / "resolved.sworld").string();
    ASSERT_TRUE(Workspace.World.SaveWorldAs(worldPath));

    ASSERT_TRUE(Workspace.World.LoadWorld(worldPath));
    EXPECT_EQ(BrushCount(), staged);
}

TEST_F(WorkspaceResolvePendingEditsTest, EscapeCancelsAStagedInsetBeforeTouchingTheSelection)
{
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    const std::size_t original = FaceCount(brush);
    const std::size_t selected = Workspace.Selection.GetSelection().size();
    Workspace.BeginInsetOnSelectedFaces(0.1f);
    ASSERT_TRUE(Workspace.HasPendingInset());

    Workspace.EscapeStep();

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_EQ(FaceCount(brush), original);
    // The first press spent itself on the preview: the selection is intact.
    EXPECT_EQ(Workspace.Selection.GetSelection().size(), selected);
}

TEST_F(WorkspaceResolvePendingEditsTest, EscapeCancelsAStagedBridge)
{
    const EntityId first = AddBrush(Vec3d{ 0, 0, 0 });
    const EntityId second = AddBrush(Vec3d{ 4, 0, 0 });
    const std::vector<SelectableRef> firstEdges = RefsOf(first, MeshElementKind::Edge);
    const std::vector<SelectableRef> secondEdges = RefsOf(second, MeshElementKind::Edge);
    ASSERT_FALSE(firstEdges.empty());
    ASSERT_FALSE(secondEdges.empty());
    Select({ firstEdges[0], secondEdges[0] });

    const std::size_t before = BrushCount();
    Workspace.Actions.Bridge(1);
    ASSERT_TRUE(Workspace.HasPendingBridge());

    Workspace.EscapeStep();

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_EQ(BrushCount(), before);
}

}
