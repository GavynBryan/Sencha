#include "WorkspaceFixture.h"

#include "document/commands/ReparentEntitiesCommand.h"
#include "workspace/HierarchyOutline.h"
#include "workspace/LevelLocation.h"
#include "workspace/PartitionOutline.h"
#include "workspace/WorldExplorer.h"

#include <zone/WorldPartitionIds.h>

#include <gtest/gtest.h>

#include <algorithm>

// The level as one outline: the world, its graphs and zones, and the entities
// of the zone being edited under that zone. A choice goes to whichever outline
// the row belongs to, and revealing a row shows it without choosing it.
namespace
{
class HierarchyOutlineTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Parent = AddBrush({ 0, 0, 0 });
        Child = AddBrush({ 2, 0, 0 });
        const EntityId children[] = { Child };
        Commands.Execute(MakeReparentEntitiesCommand(children, Parent, ReparentTransformRule::KeepWorld, Scene(),
                                                     Workspace.World.FocusDocument()));
    }

    [[nodiscard]] ExplorerPage All() { return Outline.Explore({ .Explorer = "hierarchy", .Count = 100 }); }

    [[nodiscard]] std::string KeyOf(EntityId entity) { return LevelLocationKey(Scene().PersistentIdOf(entity)); }

    [[nodiscard]] static std::ptrdiff_t IndexOf(const ExplorerPage& page, std::string_view key)
    {
        const auto found = std::ranges::find(page.Items, key, &ExplorerItem::Key);
        return found != page.Items.end() ? found - page.Items.begin() : -1;
    }

    EntityId Parent{};
    EntityId Child{};
    WorldExplorer Entities{ Workspace };
    PartitionOutline Partition{ Workspace };
    HierarchyOutline Outline{ Workspace, Entities, Partition };
};
} // namespace

TEST_F(HierarchyOutlineTest, TheFocusedZonesEntitiesSitUnderIt)
{
    const ExplorerPage page = All();
    ASSERT_FALSE(page.Items.empty());
    EXPECT_EQ(page.Items[0].Key, "world");
    const std::string focus = "zone:" + ZoneIdToString(Workspace.World.FocusZone());
    const std::ptrdiff_t zone = IndexOf(page, focus);
    const std::ptrdiff_t parent = IndexOf(page, KeyOf(Parent));
    const std::ptrdiff_t child = IndexOf(page, KeyOf(Child));
    ASSERT_GE(zone, 0);
    ASSERT_EQ(parent, zone + 1) << "the zone's entities follow it";
    ASSERT_EQ(child, parent + 1);
    EXPECT_EQ(page.Items[static_cast<std::size_t>(zone)].Branch, ExplorerBranch::Open);
    EXPECT_EQ(page.Items[static_cast<std::size_t>(parent)].Depth, page.Items[static_cast<std::size_t>(zone)].Depth + 1);
    EXPECT_EQ(page.Items[static_cast<std::size_t>(child)].Depth, page.Items[static_cast<std::size_t>(parent)].Depth + 1);
    EXPECT_TRUE(std::ranges::find(page.Actions, "entity.create") != page.Actions.end());
}

TEST_F(HierarchyOutlineTest, AChoiceGoesToTheOutlineWhoseRowItIs)
{
    Outline.Choose(KeyOf(Child), ExplorerChoice::Pick, 0);
    const SelectableRef primary = Workspace.Selection.GetPrimarySelection();
    EXPECT_TRUE(primary.IsEntity() && primary.Entity == Child);

    Outline.Choose("zone:" + ZoneIdToString(Workspace.World.FocusZone()), ExplorerChoice::Pick, 0);
    EXPECT_EQ(Workspace.World.SelectedZone(), Workspace.World.FocusZone());
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty()) << "a zone is selected in place of the entities";
}

TEST_F(HierarchyOutlineTest, RevealingShowsARowAndChoosesNothing)
{
    Outline.Choose(KeyOf(Parent), ExplorerChoice::Open, 0);
    ASSERT_EQ(IndexOf(All(), KeyOf(Child)), -1) << "closed, the parent hides its child";

    Outline.Choose(KeyOf(Child), ExplorerChoice::Reveal, 0);
    EXPECT_GE(IndexOf(All(), KeyOf(Child)), 0) << "revealed, its parent opens";
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty());

    Outline.Choose(KeyOf(Child), ExplorerChoice::Pick, 0);
    Outline.Choose("zone:" + ZoneIdToString(Workspace.World.FocusZone()), ExplorerChoice::Reveal, 0);
    EXPECT_FALSE(Workspace.Selection.GetSelection().empty()) << "revealing a zone does not select it in their place";
}
