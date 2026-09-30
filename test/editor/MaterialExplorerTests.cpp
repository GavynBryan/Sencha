#include "WorkspaceFixture.h"

#include "brush/BrushMesh.h"
#include "project/MaterialLibrary.h"
#include "workspace/MaterialExplorer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// The project's materials as an explorer: named and pictured a page at a
// time, only the page shown asked for its pictures, and choosing one puts it
// in hand -- or, put to use, on the selected faces.
namespace
{
class MaterialExplorerTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Root = std::filesystem::temp_directory_path() / ("sencha_material_explorer_" + std::to_string(std::random_device{}()));
        for (const char* name : { "brick", "concrete", "stone", "stone_wet", "tile" })
        {
            const std::filesystem::path file = Root / "materials" / (std::string(name) + ".smat");
            std::filesystem::create_directories(file.parent_path());
            std::ofstream(file) << "{}";
        }
        const std::vector<std::string> roots{ Root.string() };
        Library.Rescan(roots);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    std::filesystem::path Root;
    MaterialLibrary Library{ Logging };
    std::vector<std::string> Pictured;
    MaterialExplorer Explorer{ Library, Workspace, [this](const std::string& path) {
                                  Pictured.push_back(path);
                                  return "picture:" + path;
                              } };
};
} // namespace

TEST_F(MaterialExplorerTest, APageNamesAndPicturesOnlyWhatItShows)
{
    const ExplorerPage page = Explorer.Explore({ .Explorer = "materials", .First = 1, .Count = 2 });
    EXPECT_EQ(page.Total, 5u);
    ASSERT_EQ(page.Items.size(), 2u);
    EXPECT_EQ(page.Items[0].Label, "concrete");
    EXPECT_EQ(page.Items[0].Image, "picture:" + page.Items[0].Key);
    EXPECT_EQ(Pictured.size(), 2u) << "a picture was asked for an item no page shows";
}

TEST_F(MaterialExplorerTest, TheFilterNarrowsByName)
{
    const ExplorerPage page = Explorer.Explore({ .Explorer = "materials", .Filter = "stone", .Count = 10 });
    EXPECT_EQ(page.Total, 2u);
    const ExplorerPage none = Explorer.Explore({ .Explorer = "materials", .Filter = "glass", .Count = 10 });
    EXPECT_EQ(none.Total, 0u);
    EXPECT_FALSE(none.Empty.empty()) << "an empty page says why";
}

TEST_F(MaterialExplorerTest, PickingPutsItInHandAndPuttingItToUseAppliesIt)
{
    const EntityId brush = AddBrush({ 0, 0, 0 });
    Workspace.MeshEdit.SetElementKind(MeshElementKind::Face);
    SelectElements(brush, MeshElementKind::Face, { 0 });

    const std::string tile = Explorer.Explore({ .Explorer = "materials", .Filter = "tile", .Count = 1 }).Items.front().Key;
    Explorer.Choose(tile, ExplorerChoice::Pick);
    EXPECT_EQ(Workspace.Resources.Active(AssetType::Material).Path, tile);
    EXPECT_TRUE(Explorer.Explore({ .Explorer = "materials", .Filter = "tile", .Count = 1 }).Items.front().Current);
    EXPECT_FALSE(Commands.CanUndo()) << "picking is not an edit";

    Explorer.Choose(tile, ExplorerChoice::Commit);
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Faces[0].Material.Material.Path, tile);
    EXPECT_TRUE(Commands.CanUndo());
}
