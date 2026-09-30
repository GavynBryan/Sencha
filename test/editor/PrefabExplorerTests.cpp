#include "WorkspaceFixture.h"

#include "document/tools/PlaceTool.h"
#include "tools/ToolRegistry.h"
#include "workspace/PrefabExplorer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// The project's scene sources as prefabs to place: found under the content
// roots, pictured only as far as a page goes, and choosing one puts it in hand
// with the Place tool taken up.
namespace
{
class PrefabExplorerTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Root = std::filesystem::temp_directory_path() / ("sencha_prefab_explorer_" + std::to_string(std::random_device{}()));
        for (const char* path : { "props/crate.sscene", "props/door.sscene", "levels/hub.sscene" })
        {
            const std::filesystem::path file = Root / path;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream(file) << "{}";
        }
        std::ofstream(Root / "props/notes.txt") << "not a scene";
        Explorer.Rescan({ Root });
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    std::filesystem::path Root;
    std::vector<std::string> Pictured;
    PrefabExplorer Explorer{ Workspace, [this](const std::string& path) {
                                Pictured.push_back(path);
                                return "picture:" + path;
                            } };
};
} // namespace

TEST_F(PrefabExplorerTest, EverySceneSourceIsAPrefabNamedForItsFile)
{
    const ExplorerPage page = Explorer.Explore({ .Explorer = "prefabs", .Count = 10 });
    ASSERT_EQ(page.Total, 3u);
    EXPECT_EQ(page.Items[0].Key, "asset://levels/hub.sscene");
    EXPECT_EQ(page.Items[0].Label, "hub");
    EXPECT_EQ(Explorer.Explore({ .Explorer = "prefabs", .Filter = "door", .Count = 10 }).Total, 1u);
    EXPECT_EQ(Pictured.size(), 4u) << "each item shown was pictured once per page, and no more";
}

TEST_F(PrefabExplorerTest, ChoosingOnePutsItInHandAndTakesUpPlace)
{
    const std::string door = Explorer.Explore({ .Explorer = "prefabs", .Filter = "door", .Count = 1 }).Items.front().Key;
    Explorer.Choose(door, ExplorerChoice::Pick);
    EXPECT_EQ(Workspace.Resources.Active(AssetType::Scene).Path, door);
    ASSERT_NE(Workspace.Interaction.Tools->GetActiveTool(), nullptr);
    EXPECT_EQ(Workspace.Interaction.Tools->GetActiveTool()->GetId(), "place");
    EXPECT_TRUE(Explorer.Explore({ .Explorer = "prefabs", .Filter = "door", .Count = 1 }).Items.front().Current);
}

TEST_F(PrefabExplorerTest, PlaceConsumesAPrefabAndTurnsItByItsYaw)
{
    PlaceTool place;
    ASSERT_EQ(place.Resources().size(), 1u);
    EXPECT_EQ(place.Resources().front().Type, AssetType::Scene);
    ToolContext& ctx = Workspace.Interaction.Tools->GetContext();
    EXPECT_EQ(place.Hint(ctx), "Pick a prefab to place");
    EXPECT_EQ(place.SetParameter(ctx, "yaw", "180"), PropertyEditResult::Applied);
    EXPECT_EQ(place.SetParameter(ctx, "yaw", "45"), PropertyEditResult::Refused);
    EXPECT_EQ(place.Parameters(ctx).front().Value, "180");
}
