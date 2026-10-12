#include "WorkspaceFixture.h"

#include "document/tools/PlaceTool.h"
#include "tools/ToolRegistry.h"
#include "workspace/PrefabExplorer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>

// The project's scene sources as prefabs the shelf offers: scene files, never
// the level being edited, and choosing one puts it in hand with the Entity tool
// taken up.
namespace
{
class PrefabExplorerTest : public WorkspaceTest
{
protected:
    PrefabExplorer Explorer{ Workspace };
};
} // namespace

TEST_F(PrefabExplorerTest, OffersSceneSourcesButNotTheLevelBeingEdited)
{
    WorkspaceExplorerInfo info = Explorer.Info();
    EXPECT_EQ(info.Shape, ExplorerShape::Files);
    ASSERT_EQ(info.Accepts.size(), 1u);
    EXPECT_EQ(info.Accepts.front().Extension, ".sscene");
    EXPECT_TRUE(info.Withheld.empty()) << "an unsaved level is no file to withhold";

    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       ("sencha_prefab_explorer_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "levels");
    EditorDocument& editing = Workspace.World.FocusDocument();
    editing.SetContentRoots({ root });
    ASSERT_TRUE(editing.SaveAs((root / "levels" / "hub.sscene").string()));
    info = Explorer.Info();
    EXPECT_EQ(info.Withheld, (std::vector<std::string>{ "asset://levels/hub.sscene" }))
        << "the level is withheld by the name a placement would give it";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_F(PrefabExplorerTest, ChoosingOnePutsItInHandAndTakesUpPlace)
{
    const std::string door = "asset://props/door.sscene";
    Explorer.Choose(door, ExplorerChoice::Pick);
    EXPECT_EQ(Workspace.Resources.Active(AssetType::Scene).Path, door);
    ASSERT_NE(Workspace.Interaction.Tools->GetActiveTool(), nullptr);
    EXPECT_EQ(Workspace.Interaction.Tools->GetActiveTool()->GetId(), "place");
    EXPECT_EQ(Explorer.Info().Current, door);
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
