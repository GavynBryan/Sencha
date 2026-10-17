#include <navigation/NavPlanQueue.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    constexpr std::uint32_t kDefer = 8;

    EntityId AgentWithIndex(std::uint32_t index) { return EntityId{ index, 1 }; }
    NavRequestId RequestNumbered(std::uint32_t value) { return NavRequestId{ value }; }

    std::vector<std::uint32_t> TakeAll(NavPlanQueue& queue, std::size_t budget = 64)
    {
        std::vector<NavPlanEntry> taken;
        queue.Take(budget, [](const NavPlanEntry&) { return true; }, taken);
        std::vector<std::uint32_t> order;
        for (const NavPlanEntry& entry : taken)
            order.push_back(entry.Agent.Index);
        return order;
    }
}

TEST(NavPlanQueue, TakesByTickThenEntity)
{
    NavPlanQueue queue;
    queue.Queue(AgentWithIndex(5), RequestNumbered(1), NavPlanClass::Blocking, 2, kDefer);
    queue.Queue(AgentWithIndex(3), RequestNumbered(1), NavPlanClass::Blocking, 2, kDefer);
    queue.Queue(AgentWithIndex(9), RequestNumbered(1), NavPlanClass::Blocking, 1, kDefer);
    EXPECT_EQ(TakeAll(queue), (std::vector<std::uint32_t>{ 9, 3, 5 }));
}

TEST(NavPlanQueue, BlockingWorkGoesBeforeARecentRefreshButCannotStarveIt)
{
    NavPlanQueue queue;
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Refresh, 10, kDefer);   // ordered at 18
    queue.Queue(AgentWithIndex(2), RequestNumbered(1), NavPlanClass::Blocking, 12, kDefer);  // before it
    queue.Queue(AgentWithIndex(3), RequestNumbered(1), NavPlanClass::Blocking, 18, kDefer);  // ties at 18, after by entity
    queue.Queue(AgentWithIndex(0), RequestNumbered(1), NavPlanClass::Blocking, 19, kDefer);  // after it
    EXPECT_EQ(TakeAll(queue), (std::vector<std::uint32_t>{ 2, 1, 3, 0 }));
}

TEST(NavPlanQueue, ANewRequestStartsAtTheBack)
{
    NavPlanQueue queue;
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Blocking, 0, kDefer);
    queue.Queue(AgentWithIndex(2), RequestNumbered(1), NavPlanClass::Blocking, 3, kDefer);
    // Agent 1 is re-ordered at tick 5: the old order's place is not its to keep.
    queue.Queue(AgentWithIndex(1), RequestNumbered(2), NavPlanClass::Blocking, 5, kDefer);
    EXPECT_EQ(queue.Size(), 2u);
    EXPECT_EQ(TakeAll(queue), (std::vector<std::uint32_t>{ 2, 1 }));
}

TEST(NavPlanQueue, TheSameRequestKeepsItsPlace)
{
    NavPlanQueue queue;
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Blocking, 0, kDefer);
    queue.Queue(AgentWithIndex(2), RequestNumbered(1), NavPlanClass::Blocking, 3, kDefer);
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Blocking, 6, kDefer);
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Refresh, 7, kDefer);
    EXPECT_EQ(TakeAll(queue), (std::vector<std::uint32_t>{ 1, 2 }));
}

TEST(NavPlanQueue, ABlockingTriggerRaisesAWaitingRefresh)
{
    NavPlanQueue queue;
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Refresh, 0, kDefer);  // ordered at 8
    queue.Queue(AgentWithIndex(2), RequestNumbered(1), NavPlanClass::Blocking, 4, kDefer);
    queue.Queue(AgentWithIndex(1), RequestNumbered(1), NavPlanClass::Blocking, 2, kDefer);  // its route went invalid
    ASSERT_NE(queue.Find(AgentWithIndex(1)), nullptr);
    EXPECT_EQ(queue.Find(AgentWithIndex(1))->Class, NavPlanClass::Blocking);
    EXPECT_EQ(TakeAll(queue), (std::vector<std::uint32_t>{ 1, 2 }));
}

TEST(NavPlanQueue, DeclinedEntriesKeepTheirPlaceAndTheBudgetHolds)
{
    NavPlanQueue queue;
    for (std::uint32_t i = 0; i < 6; ++i)
        queue.Queue(AgentWithIndex(i), RequestNumbered(1), NavPlanClass::Blocking, i, kDefer);
    std::vector<NavPlanEntry> taken;
    queue.Take(2, [](const NavPlanEntry& entry) { return entry.Agent.Index != 0; }, taken);
    ASSERT_EQ(taken.size(), 2u);
    EXPECT_EQ(taken[0].Agent.Index, 1u);
    EXPECT_EQ(taken[1].Agent.Index, 2u);
    EXPECT_EQ(TakeAll(queue), (std::vector<std::uint32_t>{ 0, 3, 4, 5 }));
}
