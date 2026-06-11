#include <gtest/gtest.h>

#include <jobs/AsyncTaskQueue.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    // Polls the owner-thread drain until the task completes or the deadline
    // expires. Threaded tests only; zero-thread tests use PumpWork instead.
    bool DrainUntilComplete(AsyncTaskQueue& queue, const AsyncTaskHandle& handle,
                            std::chrono::seconds deadline = std::chrono::seconds(10))
    {
        const auto start = std::chrono::steady_clock::now();
        while (!queue.IsComplete(handle))
        {
            queue.DrainCompletions();
            if (std::chrono::steady_clock::now() - start > deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
}

//=============================================================================
// Zero-thread mode: Submit -> PumpWork -> DrainCompletions is fully
// deterministic. This is the configuration unit tests and engine tests use.
//=============================================================================

TEST(AsyncTaskQueueZeroThread, SubmitAloneRunsNothing)
{
    AsyncTaskQueue queue(0);
    bool workRan = false;
    bool commitRan = false;

    auto handle = queue.Submit<int>(
        [&] { workRan = true; return 1; },
        [&](int) { commitRan = true; });

    EXPECT_FALSE(workRan);
    EXPECT_FALSE(commitRan);
    EXPECT_EQ(queue.GetState(handle), AsyncTaskState::Pending);
}

TEST(AsyncTaskQueueZeroThread, PumpRunsWorkButNotCommit)
{
    AsyncTaskQueue queue(0);
    bool workRan = false;
    bool commitRan = false;

    auto handle = queue.Submit<int>(
        [&] { workRan = true; return 1; },
        [&](int) { commitRan = true; });

    EXPECT_EQ(queue.PumpWork(), 1u);
    EXPECT_TRUE(workRan);
    EXPECT_FALSE(commitRan);
    EXPECT_EQ(queue.GetState(handle), AsyncTaskState::AwaitingCommit);
    EXPECT_FALSE(queue.IsComplete(handle));
}

TEST(AsyncTaskQueueZeroThread, DrainRunsCommitsWithPayloadInCompletionOrder)
{
    AsyncTaskQueue queue(0);
    std::vector<int> committed;

    for (int i = 0; i < 4; ++i)
    {
        queue.Submit<int>([i] { return i * 10; },
                          [&committed](int v) { committed.push_back(v); });
    }

    EXPECT_EQ(queue.PumpWork(), 4u);
    EXPECT_EQ(queue.DrainCompletions(), 4u);
    ASSERT_EQ(committed.size(), 4u);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(committed[i], i * 10);
    }
}

TEST(AsyncTaskQueueZeroThread, CommitCountBudgetIsAHardCap)
{
    AsyncTaskQueue queue(0);
    int commits = 0;
    std::vector<AsyncTaskHandle> handles;
    for (int i = 0; i < 5; ++i)
    {
        handles.push_back(queue.Submit<int>([] { return 0; }, [&](int) { ++commits; }));
    }
    queue.PumpWork();

    EXPECT_EQ(queue.DrainCompletions({ .MaxCommits = 2 }), 2u);
    EXPECT_EQ(commits, 2);
    EXPECT_TRUE(queue.IsComplete(handles[1]));
    EXPECT_FALSE(queue.IsComplete(handles[2]));

    EXPECT_EQ(queue.DrainCompletions(), 3u);
    EXPECT_EQ(commits, 5);
    EXPECT_TRUE(queue.IsComplete(handles[4]));
}

TEST(AsyncTaskQueueZeroThread, ZeroCommitBudgetDrainsNothing)
{
    AsyncTaskQueue queue(0);
    bool commitRan = false;
    auto handle = queue.Submit<int>([] { return 0; }, [&](int) { commitRan = true; });
    queue.PumpWork();

    // MaxCommits = 0 is the explicit pause: nothing runs, nothing is lost.
    EXPECT_EQ(queue.DrainCompletions({ .MaxCommits = 0 }), 0u);
    EXPECT_FALSE(commitRan);
    EXPECT_EQ(queue.GetState(handle), AsyncTaskState::AwaitingCommit);

    EXPECT_EQ(queue.DrainCompletions(), 1u);
    EXPECT_TRUE(commitRan);
}

TEST(AsyncTaskQueueZeroThread, TimeBudgetRunsFirstCommitThenStops)
{
    AsyncTaskQueue queue(0);
    int commits = 0;
    for (int i = 0; i < 3; ++i)
    {
        queue.Submit<int>([] { return 0; },
                          [&](int) {
                              ++commits;
                              // Each commit alone exceeds the drain budget below.
                              std::this_thread::sleep_for(std::chrono::milliseconds(5));
                          });
    }
    queue.PumpWork();

    // MaxTime is soft: the first commit always runs; after it the budget is
    // spent, so each drain advances by exactly one commit.
    const AsyncDrainBudget budget{ .MaxTime = std::chrono::milliseconds(1) };
    EXPECT_EQ(queue.DrainCompletions(budget), 1u);
    EXPECT_EQ(commits, 1);
    EXPECT_EQ(queue.DrainCompletions(budget), 1u);
    EXPECT_EQ(commits, 2);
    EXPECT_EQ(queue.DrainCompletions(budget), 1u);
    EXPECT_EQ(commits, 3);
    EXPECT_EQ(queue.DrainCompletions(budget), 0u);
}

TEST(AsyncTaskQueueZeroThread, TimeBudgetAllowsManyCheapCommits)
{
    AsyncTaskQueue queue(0);
    int commits = 0;
    for (int i = 0; i < 50; ++i)
    {
        queue.Submit<int>([] { return 0; }, [&](int) { ++commits; });
    }
    queue.PumpWork();

    // Cheap commits never exhaust a generous budget: one drain takes all.
    EXPECT_EQ(queue.DrainCompletions({ .MaxTime = std::chrono::seconds(10) }), 50u);
    EXPECT_EQ(commits, 50);
}

TEST(AsyncTaskQueueZeroThread, PumpBudgetIsRespected)
{
    AsyncTaskQueue queue(0);
    int worked = 0;
    for (int i = 0; i < 3; ++i)
    {
        queue.Submit<int>([&] { return ++worked; }, [](int) {});
    }

    EXPECT_EQ(queue.PumpWork(1), 1u);
    EXPECT_EQ(worked, 1);
    EXPECT_EQ(queue.PumpWork(), 2u);
    EXPECT_EQ(worked, 3);
}

TEST(AsyncTaskQueueZeroThread, MoveOnlyPayloadMovesThrough)
{
    AsyncTaskQueue queue(0);
    int committedValue = 0;

    queue.Submit<std::unique_ptr<int>>(
        [] { return std::make_unique<int>(42); },
        [&](std::unique_ptr<int> payload) { committedValue = *payload; });

    queue.PumpWork();
    queue.DrainCompletions();
    EXPECT_EQ(committedValue, 42);
}

TEST(AsyncTaskQueueZeroThread, CancelBeforePumpSkipsWork)
{
    AsyncTaskQueue queue(0);
    bool workRan = false;

    auto handle = queue.Submit<int>([&] { workRan = true; return 0; }, [](int) {});

    EXPECT_TRUE(queue.Cancel(handle));
    EXPECT_EQ(queue.GetState(handle), AsyncTaskState::Cancelled);
    EXPECT_EQ(queue.PumpWork(), 0u);
    EXPECT_FALSE(workRan);
    EXPECT_EQ(queue.DrainCompletions(), 0u);
    EXPECT_FALSE(queue.IsComplete(handle));
}

TEST(AsyncTaskQueueZeroThread, CancelAfterPumpDropsCommit)
{
    AsyncTaskQueue queue(0);
    bool commitRan = false;

    auto handle = queue.Submit<int>([] { return 0; }, [&](int) { commitRan = true; });

    queue.PumpWork();
    EXPECT_TRUE(queue.Cancel(handle));
    EXPECT_EQ(queue.DrainCompletions(), 0u);
    EXPECT_FALSE(commitRan);
    EXPECT_EQ(queue.GetState(handle), AsyncTaskState::Cancelled);
}

TEST(AsyncTaskQueueZeroThread, CancelAfterCommitFails)
{
    AsyncTaskQueue queue(0);
    auto handle = queue.Submit<int>([] { return 0; }, [](int) {});
    queue.PumpWork();
    queue.DrainCompletions();

    EXPECT_TRUE(queue.IsComplete(handle));
    EXPECT_FALSE(queue.Cancel(handle));
    EXPECT_EQ(queue.GetState(handle), AsyncTaskState::Committed);
}

TEST(AsyncTaskQueueZeroThread, CommitMaySubmitFollowUpTasks)
{
    AsyncTaskQueue queue(0);
    bool followUpCommitted = false;

    queue.Submit<int>(
        [] { return 0; },
        [&](int) {
            queue.Submit<int>([] { return 1; },
                              [&](int) { followUpCommitted = true; });
        });

    queue.PumpWork();
    queue.DrainCompletions();
    EXPECT_FALSE(followUpCommitted);  // follow-up only submitted, not pumped

    queue.PumpWork();
    queue.DrainCompletions();
    EXPECT_TRUE(followUpCommitted);
}

//=============================================================================
// Threaded mode: one smoke test for the request/poll shape (the same pattern
// as the SwapchainRebuildWorker coverage) plus thread-identity checks.
//=============================================================================

TEST(AsyncTaskQueueThreaded, WorkRunsOffThreadCommitRunsOnOwner)
{
    AsyncTaskQueue queue(1);
    EXPECT_EQ(queue.WorkerCount(), 1u);

    const auto ownerId = std::this_thread::get_id();
    std::thread::id workId;
    std::thread::id commitId;

    auto handle = queue.Submit<int>(
        [&] { workId = std::this_thread::get_id(); return 7; },
        [&](int v) {
            commitId = std::this_thread::get_id();
            EXPECT_EQ(v, 7);
        });

    ASSERT_TRUE(DrainUntilComplete(queue, handle));
    EXPECT_NE(workId, ownerId);
    EXPECT_EQ(commitId, ownerId);
}

TEST(AsyncTaskQueueThreaded, ManyTasksAllCommit)
{
    AsyncTaskQueue queue(1);
    constexpr int TaskCount = 100;

    int sum = 0;
    std::vector<AsyncTaskHandle> handles;
    for (int i = 0; i < TaskCount; ++i)
    {
        handles.push_back(queue.Submit<int>([i] { return i; },
                                            [&sum](int v) { sum += v; }));
    }

    ASSERT_TRUE(DrainUntilComplete(queue, handles.back()));
    // Completion order is FIFO through a single worker, so the last handle
    // completing implies all of them did.
    for (const auto& handle : handles)
    {
        EXPECT_TRUE(queue.IsComplete(handle));
    }
    EXPECT_EQ(sum, TaskCount * (TaskCount - 1) / 2);
}

TEST(AsyncTaskQueueThreaded, DestructorWithPendingAndUndrainedTasksDoesNotHang)
{
    bool commitRan = false;
    {
        AsyncTaskQueue queue(1);
        auto first = queue.Submit<int>([] { return 0; }, [&](int) { commitRan = true; });
        queue.Submit<int>(
            [] {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return 0;
            },
            [&](int) { commitRan = true; });
        (void)first;
        // No pump, no drain: destructor joins the worker and drops everything.
    }
    EXPECT_FALSE(commitRan);
}

//=============================================================================
// Contract death tests, matching the engine's assert-based pattern.
//=============================================================================

TEST(AsyncTaskQueueContracts, PumpWorkOnThreadedQueueDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue queue(1);
    EXPECT_DEATH((void)queue.PumpWork(), "zero-worker");
}

TEST(AsyncTaskQueueContracts, ThrowingWorkDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue queue(0);
    queue.Submit<int>([]() -> int { throw 42; }, [](int) {});
    EXPECT_DEATH((void)queue.PumpWork(), "must not throw");
}

TEST(AsyncTaskQueueContracts, SubmitFromNonOwnerThreadDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue queue(0);
    EXPECT_DEATH(
        {
            std::thread other([&] {
                queue.Submit<int>([] { return 0; }, [](int) {});
            });
            other.join();
        },
        "owner-thread-only");
}
