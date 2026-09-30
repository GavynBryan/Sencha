#include <core/handle/HandlePool.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    using Pool = HandlePool<struct HandlePoolTestTag, int>;

    struct Pinned
    {
        explicit Pinned(int value) : Value(value) {}
        Pinned(const Pinned&) = delete;
        Pinned& operator=(const Pinned&) = delete;
        int Value = 0;
    };
}

TEST(HandlePool, AHandleFindsItsValueUntilErased)
{
    Pool pool;
    const Pool::HandleType a = pool.Emplace(7);
    ASSERT_TRUE(a.IsValid());
    ASSERT_NE(pool.Find(a), nullptr);
    EXPECT_EQ(*pool.Find(a), 7);
    EXPECT_TRUE(pool.Erase(a));
    EXPECT_EQ(pool.Find(a), nullptr);
    EXPECT_FALSE(pool.Erase(a));
    EXPECT_EQ(pool.Size(), 0u);
}

TEST(HandlePool, AStaleHandleNeverReachesTheSlotsNextValue)
{
    Pool pool;
    const Pool::HandleType first = pool.Emplace(1);
    pool.Erase(first);
    const Pool::HandleType second = pool.Emplace(2);
    EXPECT_EQ(second.Index, first.Index);
    EXPECT_NE(second.Generation, first.Generation);
    EXPECT_EQ(pool.Find(first), nullptr);
    EXPECT_EQ(*pool.Find(second), 2);
}

TEST(HandlePool, ClearedHandlesStayStale)
{
    Pool pool;
    const Pool::HandleType before = pool.Emplace(1);
    pool.Clear();
    const Pool::HandleType after = pool.Emplace(2);
    EXPECT_EQ(pool.Find(before), nullptr);
    EXPECT_EQ(*pool.Find(after), 2);
}

TEST(HandlePool, NullHandleResolvesNothing)
{
    Pool pool;
    pool.Emplace(1);
    EXPECT_EQ(pool.Find(Pool::HandleType{}), nullptr);
}

TEST(HandlePool, IteratesLiveValuesInSlotOrder)
{
    Pool pool;
    const Pool::HandleType a = pool.Emplace(1);
    const Pool::HandleType b = pool.Emplace(2);
    pool.Emplace(3);
    pool.Erase(b);
    std::vector<int> seen;
    pool.ForEach([&](Pool::HandleType, int value) { seen.push_back(value); });
    EXPECT_EQ(seen, (std::vector<int>{ 1, 3 }));
    EXPECT_TRUE(pool.Contains(a));
}

TEST(HandlePool, HoldsValuesThatCannotMoveAndReleasesThem)
{
    HandlePool<struct PinnedTag, Pinned> pool;
    const auto handle = pool.Emplace(5);
    const Pinned* address = pool.Find(handle);
    for (int i = 0; i < 64; ++i)
        pool.Emplace(i);
    EXPECT_EQ(pool.Find(handle), address);
    std::unique_ptr<Pinned> released = pool.Release(handle);
    ASSERT_NE(released, nullptr);
    EXPECT_EQ(released->Value, 5);
    EXPECT_EQ(pool.Find(handle), nullptr);
}
