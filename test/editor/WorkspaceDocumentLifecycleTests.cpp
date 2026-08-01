#include "WorkspaceFixture.h"

#include <filesystem>
#include <fstream>

// Document replacement versus live preview state. Every path that swaps the
// edited documents destroys the ones a preview staged into, so the unwind has
// to happen before the destruction, not after it. Also covers the other side of
// the same boundary: a file that fails to load must not cost the open session.
namespace
{

class WorkspaceDocumentLifecycleTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Dir = std::filesystem::temp_directory_path() / "sencha_workspace_lifecycle_tests";
        std::filesystem::remove_all(Dir);
        std::filesystem::create_directories(Dir);
    }

    void TearDown() override { std::filesystem::remove_all(Dir); }

    [[nodiscard]] std::string WriteFile(const char* name, const char* contents)
    {
        const std::filesystem::path path = Dir / name;
        std::ofstream file(path);
        file << contents;
        file.close();
        return path.string();
    }

    // A brush with one face selected and an inset previewing over it.
    EntityId BeginPendingInset()
    {
        const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
        SelectElements(brush, MeshElementKind::Face, { 0 });
        Workspace.BeginInsetOnSelectedFaces(0.1f);
        EXPECT_TRUE(Workspace.HasPendingInset());
        return brush;
    }

    // Two brushes bridged into a live preview entity.
    void BeginPendingBridge()
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
    }

    std::filesystem::path Dir;
};

TEST_F(WorkspaceDocumentLifecycleTest, NewWorldUnwindsAPendingInset)
{
    BeginPendingInset();

    Workspace.World.NewWorld("Replacement");

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(WorkspaceDocumentLifecycleTest, NewWorldUnwindsAPendingBridge)
{
    BeginPendingBridge();

    Workspace.World.NewWorld("Replacement");

    EXPECT_FALSE(Workspace.HasPendingBridge());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspaceDocumentLifecycleTest, ClosingAWorldToLegacyUnwindsAPendingEdit)
{
    BeginPendingInset();

    Workspace.World.New(); // world mode -> fresh legacy document

    EXPECT_FALSE(Workspace.World.IsWorld());
    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspaceDocumentLifecycleTest, LoadingAWorldUnwindsAPendingEdit)
{
    // Save the current world so there is a real manifest to load back.
    const std::string worldPath = (Dir / "world.sworld").string();
    ASSERT_TRUE(Workspace.World.SaveWorldAs(worldPath));

    BeginPendingInset();

    ASSERT_TRUE(Workspace.World.LoadWorld(worldPath));

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspaceDocumentLifecycleTest, LoadingALegacyDocumentUnwindsAPendingEdit)
{
    // A legacy scene file produced by the editor itself.
    Workspace.World.New();
    const std::string scenePath = (Dir / "scene.json").string();
    ASSERT_TRUE(Workspace.World.SaveAs(scenePath));

    BeginPendingInset();

    ASSERT_TRUE(Workspace.World.Load(scenePath));

    EXPECT_FALSE(Workspace.HasPendingInset());
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspaceDocumentLifecycleTest, AFailedLegacyLoadKeepsTheOpenDocument)
{
    Workspace.World.New();
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    ASSERT_TRUE(Scene().TryGetBrushMesh(brush) != nullptr);
    const std::size_t before = BrushCount();

    // Valid JSON the scene loader rejects (unsupported version).
    const std::string badPath = WriteFile("bad.json", R"({"version":99,"entities":[]})");
    EXPECT_FALSE(Workspace.World.Load(badPath));

    EXPECT_EQ(BrushCount(), before);
    EXPECT_NE(Scene().TryGetBrushMesh(brush), nullptr);
}

TEST_F(WorkspaceDocumentLifecycleTest, AFailedLegacyLoadKeepsTheOpenWorld)
{
    ASSERT_TRUE(Workspace.World.IsWorld());
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    const std::size_t before = BrushCount();

    const std::string badPath = WriteFile("bad.json", R"({"version":99,"entities":[]})");
    EXPECT_FALSE(Workspace.World.Load(badPath));

    // The world did not close behind the rejected file.
    EXPECT_TRUE(Workspace.World.IsWorld());
    EXPECT_EQ(BrushCount(), before);
    EXPECT_NE(Scene().TryGetBrushMesh(brush), nullptr);
}

TEST_F(WorkspaceDocumentLifecycleTest, AFailedLegacyLoadLeavesAPendingEditAlone)
{
    Workspace.World.New();
    BeginPendingInset();

    const std::string badPath = WriteFile("bad.json", R"({"version":99,"entities":[]})");
    EXPECT_FALSE(Workspace.World.Load(badPath));

    // Nothing was replaced, so the transaction is still the user's to resolve.
    EXPECT_TRUE(Workspace.HasPendingInset());
    EXPECT_TRUE(Commands.HasPendingEdit());
}

}
