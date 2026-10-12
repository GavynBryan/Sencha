#include "workspace/LevelLocation.h"
#include "workspaces/TreeOutline.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

// An outline over nodes in preorder: branches are as they start until flipped,
// what is current is revealed, a filter keeps the branches above a match, and
// it pages through what shows.
namespace
{
std::vector<OutlineNode> Tree()
{
    return {
        { .Key = "a", .Label = "Atrium", .Depth = 0 },
        { .Key = "a/b", .Label = "Bench", .Depth = 1 },
        { .Key = "a/c", .Label = "Crate", .Depth = 1, .StartsClosed = true },
        { .Key = "a/c/d", .Label = "Door", .Depth = 2 },
        { .Key = "e", .Label = "Exit", .Depth = 0, .Marks = { IconId::Eye } },
    };
}

std::vector<std::string> Keys(const ExplorerPage& page)
{
    std::vector<std::string> keys;
    for (const ExplorerItem& item : page.Items)
        keys.push_back(item.Key);
    return keys;
}

ExplorerPage All(TreeOutline& outline, const std::vector<OutlineNode>& nodes, std::string filter = {})
{
    return outline.Explore(nodes, { .Explorer = "tree", .Filter = std::move(filter), .Count = 100 });
}
} // namespace

TEST(TreeOutline, ABranchIsAsItStartsUntilFlipped)
{
    TreeOutline outline;
    const std::vector<OutlineNode> nodes = Tree();
    EXPECT_EQ(Keys(All(outline, nodes)), (std::vector<std::string>{ "a", "a/b", "a/c", "e" }))
        << "a branch that starts closed shows itself alone";
    outline.Flip("a/c");
    EXPECT_EQ(Keys(All(outline, nodes)), (std::vector<std::string>{ "a", "a/b", "a/c", "a/c/d", "e" }));
    outline.Flip("a");
    EXPECT_EQ(Keys(All(outline, nodes)), (std::vector<std::string>{ "a", "e" }));
    EXPECT_EQ(All(outline, nodes).Items.back().Marks, (std::vector<IconId>{ IconId::Eye }));
}

TEST(TreeOutline, WhatIsCurrentOrRevealedHasTheBranchesAboveItOpened)
{
    TreeOutline outline;
    std::vector<OutlineNode> nodes = Tree();
    outline.Flip("a");
    nodes[3].Current = true;
    const std::vector<std::string> shown = Keys(All(outline, nodes));
    EXPECT_NE(std::ranges::find(shown, "a/c/d"), shown.end())
        << "a selection made elsewhere is shown, opening even a branch that starts closed";

    nodes[3].Current = false;
    outline.Flip("a");
    EXPECT_EQ(Keys(All(outline, nodes)).size(), 2u);
    outline.Reveal("a/b");
    EXPECT_EQ(Keys(All(outline, nodes))[1], "a/b");
}

TEST(TreeOutline, AFilterKeepsAMatchAndWhatIsAboveIt)
{
    TreeOutline outline;
    EXPECT_EQ(Keys(All(outline, Tree(), "Door")), (std::vector<std::string>{ "a", "a/c", "a/c/d" }));
}

TEST(LevelLocation, EveryPlaceIsWrittenAndReadBack)
{
    const std::vector<LevelLocation> places = { WorldScenePlace{}, GraphId{ 0x2 }, ZoneId{ 0x3 }, TransitionId{ 0x4 },
                                                PersistentEntityId{ 0x5 } };
    for (const LevelLocation& place : places)
    {
        const std::optional<LevelLocation> read = ParseLevelLocation(LevelLocationKey(place));
        ASSERT_TRUE(read.has_value()) << LevelLocationKey(place);
        EXPECT_EQ(*read, place) << LevelLocationKey(place);
    }
    EXPECT_FALSE(ParseLevelLocation("zone:not-hex").has_value());
    EXPECT_FALSE(ParseLevelLocation("elsewhere").has_value());
}
