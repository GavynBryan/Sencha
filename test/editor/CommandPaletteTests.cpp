#include "shell/CommandPalette.h"
#include "text/TextFilterMatch.h"

#include <gtest/gtest.h>

#include <vector>

TEST(TextFilterRank, TheStartBeatsAWordBeatsAnywhere)
{
    EXPECT_EQ(TextFilterRank("Save All", "save"), 0);
    EXPECT_EQ(TextFilterRank("Cook From Scratch", "from"), 1);
    EXPECT_EQ(TextFilterRank("Dissolve Edges", "olve"), 2);
    EXPECT_EQ(TextFilterRank("Undo", "redo"), std::nullopt);
    EXPECT_EQ(TextFilterRank("Anything", ""), 0) << "an empty query matches everything";
    EXPECT_EQ(TextFilterRank("Resave", "save"), 2);
    EXPECT_EQ(TextFilterRank("Resave Save", "save"), 1) << "the best place it matches counts";
}

TEST(CommandPalette, WhatTheQueryNamesComesFirst)
{
    const std::vector<CommandCandidate> candidates = {
        { "file.save_all", "Save All", "File" },
        { "edit.dissolve", "Dissolve Edges", "Edit" },
        { "file.save", "Save", "File" },
        { "grid.reset", "Reset Grid", "Grid" },
        { "open:level", "Go to Level", "Workspace" },
    };
    const std::vector<std::size_t> save = RankCommands(candidates, "sa", 10);
    ASSERT_EQ(save.size(), 2u);
    EXPECT_EQ(candidates[save[0]].Key, "file.save_all") << "equal matches keep their order";
    EXPECT_EQ(candidates[save[1]].Key, "file.save");

    const std::vector<std::size_t> grid = RankCommands(candidates, "grid", 10);
    ASSERT_EQ(grid.size(), 1u);
    EXPECT_EQ(candidates[grid[0]].Key, "grid.reset");

    const std::vector<std::size_t> edit = RankCommands(candidates, "edit", 10);
    ASSERT_EQ(edit.size(), 1u) << "a category match still finds what a label does not";
    EXPECT_EQ(candidates[edit[0]].Key, "edit.dissolve");

    EXPECT_EQ(RankCommands(candidates, "", 2).size(), 2u) << "the limit holds";
}
