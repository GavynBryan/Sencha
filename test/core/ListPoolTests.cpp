#include <core/containers/ListPool.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    using Lists = ListPool<struct ListPoolTestTag, int>;
    using List = Lists::HandleType;

    List MadeAndRetained(Lists& lists, std::vector<int> items)
    {
        const List list = lists.Make(items);
        lists.Retain(list);
        return list;
    }
}

TEST(ListPool, ARowThatNamesNoListReadsEmptyAndGetsItsOwnOnTheFirstEdit)
{
    Lists lists;
    List row;
    EXPECT_TRUE(lists.Read(row).empty());
    EXPECT_EQ(lists.Stats().Live, 0u);

    lists.Edit(row).push_back(7);
    EXPECT_TRUE(row.IsValid());
    EXPECT_EQ(lists.Read(row).size(), 1u);
    EXPECT_EQ(lists.Stats().Live, 1u);

    lists.Release(row);
    EXPECT_EQ(lists.Stats().Live, 0u) << "the row's own reference was its only one";
    EXPECT_TRUE(lists.Read(row).empty()) << "a freed list's handle names nothing";
}

TEST(ListPool, AListTwoRowsNameIsCopiedBeforeOneOfThemWritesIt)
{
    Lists lists;
    List first = MadeAndRetained(lists, { 1, 2, 3 });
    List second = first;
    lists.Retain(second);

    lists.Edit(second).push_back(4);
    EXPECT_NE(first, second);
    EXPECT_EQ(lists.Read(first).size(), 3u) << "the other row's list is as it was";
    EXPECT_EQ(lists.Read(second).size(), 4u);
    EXPECT_EQ(lists.Stats().Live, 2u);

    lists.Release(first);
    lists.Release(second);
    EXPECT_EQ(lists.Stats().Live, 0u);
}

TEST(ListPool, AListIsKeptUntilItsLastReferenceGoes)
{
    Lists lists;
    const List list = MadeAndRetained(lists, { 5 });
    lists.Retain(list);
    lists.Release(list);
    EXPECT_EQ(lists.Read(list).size(), 1u);
    lists.Release(list);
    EXPECT_TRUE(lists.Read(list).empty());
    lists.Release(list);
    EXPECT_EQ(lists.Stats().Live, 0u) << "releasing a freed list does nothing";
}

TEST(ListPool, AListNothingRetainedIsDroppedAndOneRetainedIsNot)
{
    Lists lists;
    const List adopted = lists.Make(std::vector<int>{ 1 });
    const List abandoned = lists.Make(std::vector<int>{ 2 });
    EXPECT_EQ(lists.Stats().Unreferenced, 2u);
    lists.Retain(adopted);
    EXPECT_EQ(lists.Stats().Unreferenced, 1u);

    lists.DropUnreferenced();
    EXPECT_EQ(lists.Read(adopted).size(), 1u);
    EXPECT_TRUE(lists.Read(abandoned).empty());
    EXPECT_EQ(lists.Stats().Live, 1u);
    EXPECT_EQ(lists.Stats().Unreferenced, 0u);
}

TEST(ListPool, FreedListsAreReusedUpToTheRetentionLimitAndTheRestGoBack)
{
    Lists lists;
    const std::size_t kept = Lists::kRetention.Limit(0);
    std::vector<List> made;
    for (std::size_t i = 0; i < kept + 1; ++i)
        made.push_back(MadeAndRetained(lists, { static_cast<int>(i) }));
    EXPECT_EQ(lists.Stats().Allocated, kept + 1);
    EXPECT_EQ(lists.Stats().PeakLive, kept + 1);
    for (const List list : made)
        lists.Release(list);
    EXPECT_EQ(lists.Stats().Live, 0u);
    EXPECT_EQ(lists.Stats().Spare, kept) << "one past the limit went back to the allocator";

    for (std::size_t i = 0; i < kept; ++i)
        (void)MadeAndRetained(lists, { 1 });
    EXPECT_EQ(lists.Stats().Allocated, kept + 1) << "a warm pool allocates nothing";
    EXPECT_EQ(lists.Stats().Reused, kept);
    (void)MadeAndRetained(lists, { 1 });
    EXPECT_EQ(lists.Stats().Allocated, kept + 2);
}
