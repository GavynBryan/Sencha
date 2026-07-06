#include "ui/ThumbnailEviction.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

TEST(ThumbnailEviction, UnderBudgetEvictsNothing)
{
    const std::vector<std::uint64_t> lastUsed{ 1, 2, 3 };
    EXPECT_TRUE(SelectThumbnailEvictions(lastUsed, /*currentFrame*/ 3, /*budget*/ 3).empty());
    EXPECT_TRUE(SelectThumbnailEvictions(lastUsed, 3, 10).empty());
    EXPECT_TRUE(SelectThumbnailEvictions({}, 3, 0).empty());
}

TEST(ThumbnailEviction, OverBudgetEvictsOldestFirst)
{
    // Frames: index 2 is oldest, then 0, then 3, then 1 (current).
    const std::vector<std::uint64_t> lastUsed{ 5, 9, 2, 7 };
    const std::vector<std::size_t> evicted = SelectThumbnailEvictions(lastUsed, /*currentFrame*/ 9, /*budget*/ 2);
    ASSERT_EQ(evicted.size(), 2u);
    EXPECT_EQ(evicted[0], 2u);
    EXPECT_EQ(evicted[1], 0u);
}

TEST(ThumbnailEviction, EntriesUsedThisFrameAreNeverEvicted)
{
    // Every entry was drawn this frame; over budget or not, nothing goes.
    const std::vector<std::uint64_t> allCurrent{ 4, 4, 4, 4 };
    EXPECT_TRUE(SelectThumbnailEvictions(allCurrent, /*currentFrame*/ 4, /*budget*/ 1).empty());

    // Mixed: only the stale entries are candidates, even if that leaves the
    // cache above budget.
    const std::vector<std::uint64_t> mixed{ 4, 1, 4, 2, 4 };
    const std::vector<std::size_t> evicted = SelectThumbnailEvictions(mixed, 4, /*budget*/ 1);
    ASSERT_EQ(evicted.size(), 2u);
    EXPECT_EQ(evicted[0], 1u);
    EXPECT_EQ(evicted[1], 3u);
    EXPECT_TRUE(std::none_of(evicted.begin(), evicted.end(),
                             [&](std::size_t i) { return mixed[i] == 4; }));
}

TEST(ThumbnailEviction, EqualAgesBreakTiesByIndex)
{
    const std::vector<std::uint64_t> lastUsed{ 3, 3, 3, 3 };
    const std::vector<std::size_t> evicted = SelectThumbnailEvictions(lastUsed, /*currentFrame*/ 8, /*budget*/ 2);
    ASSERT_EQ(evicted.size(), 2u);
    EXPECT_EQ(evicted[0], 0u);
    EXPECT_EQ(evicted[1], 1u);
}
