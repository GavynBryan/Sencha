#include "WorkspaceFixture.h"

#include "problems/ProblemList.h"
#include "workspace/PartitionOutline.h"

#include <gtest/gtest.h>

#include <algorithm>

// A world's partition as an outline and as properties: zones are selected and
// focused from it, a graph's streaming shape is set as a choice over its
// radius, every manifest edit is a step, and the world's validation arrives
// as located problems that reveal what they are about.
namespace
{
class PartitionOutlineTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ExplorerPage All() const { return Outline.Explore({ .Explorer = "zones", .Count = 50 }); }

    [[nodiscard]] std::string KeyStartingWith(std::string_view prefix) const
    {
        for (const ExplorerItem& item : All().Items)
            if (item.Key.starts_with(prefix))
                return item.Key;
        return {};
    }

    PropertyEditResult Commit(std::string_view key, std::string_view value)
    {
        return Outline.BeginEdit(key) ? Outline.CommitEdit(key, value) : PropertyEditResult::Refused;
    }

    PartitionOutline Outline{ Workspace };
};
} // namespace

TEST_F(PartitionOutlineTest, TheWorldItsGraphsAndTheirZonesAreTheOutline)
{
    const ExplorerPage page = All();
    ASSERT_GE(page.Items.size(), 3u);
    EXPECT_EQ(page.Items[0].Key, "world");
    EXPECT_TRUE(page.Items[1].Key.starts_with("graph:"));
    EXPECT_TRUE(page.Items[2].Key.starts_with("zone:"));
    EXPECT_EQ(page.Items[2].Depth, 1);
}

TEST_F(PartitionOutlineTest, ChoosingAZoneSelectsItAndItsPropertiesAreTheCard)
{
    const std::string zone = KeyStartingWith("zone:");
    Outline.Choose(zone, ExplorerChoice::Pick, 0);
    EXPECT_TRUE(Workspace.World.SelectedZone().IsValid());
    EXPECT_FALSE(Outline.Title().empty());
    EXPECT_EQ(Commit("name", "Atrium"), PropertyEditResult::Applied);
    EXPECT_EQ(Outline.Title(), "Atrium");
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_NE(Outline.Title(), "Atrium");
}

TEST_F(PartitionOutlineTest, AGraphsShapeIsAChoiceOverItsRadius)
{
    Outline.Choose(KeyStartingWith("graph:"), ExplorerChoice::Pick, 0);
    const GraphId graph = Workspace.World.Manifest().Graphs.front().Id;
    EXPECT_EQ(Commit("shape", "Graph"), PropertyEditResult::Applied);
    EXPECT_EQ(Workspace.World.Manifest().Graphs.front().Streaming.Radius, 0.0);
    EXPECT_EQ(Commit("shape", "Proximity"), PropertyEditResult::Applied);
    EXPECT_GT(*Workspace.World.Manifest().Graphs.front().Streaming.Radius, 0.0);
    EXPECT_EQ(Commit("radius", ""), PropertyEditResult::Applied) << "emptied, it inherits again";
    EXPECT_FALSE(Workspace.World.Manifest().Graphs.front().Streaming.Radius.has_value());
    EXPECT_EQ(Outline.Act("graph", "add_zone", {}), PropertyEditResult::Applied);
    EXPECT_EQ(std::ranges::count(Workspace.World.Manifest().Zones, graph, &ZoneHeader::Graph), 2);
}

TEST_F(PartitionOutlineTest, AProblemRevealsTheZoneItIsAbout)
{
    const std::string zoneKey = KeyStartingWith("zone:");
    Workspace.Selection.ClearSelection();
    ASSERT_TRUE(Outline.Reveal(zoneKey));
    EXPECT_TRUE(Workspace.World.SelectedZone().IsValid());
    EXPECT_FALSE(Outline.Reveal("nothing:1"));
    for (const Problem& problem : Outline.Problems({}))
        EXPECT_FALSE(problem.Location.empty()) << "every problem says where it is";
}
