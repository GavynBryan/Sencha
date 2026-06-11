#include <gtest/gtest.h>

#include <jobs/ThreadPoolJobSystem.h>

#include <atomic>
#include <latch>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

//=============================================================================
// Deterministic behavior: ThreadPoolJobSystem(0) runs every job inline on
// the calling thread in index order. This configuration is the reference
// implementation the threaded configurations are compared against.
//=============================================================================

TEST(JobSystemZeroWorkers, RunsAllJobsInlineInIndexOrder)
{
    ThreadPoolJobSystem jobs(0);
    EXPECT_EQ(jobs.WorkerCount(), 0u);

    std::vector<uint32_t> order;
    const auto callerId = std::this_thread::get_id();
    bool allOnCaller = true;

    jobs.ParallelFor(8, [&](uint32_t i) {
        order.push_back(i);
        allOnCaller = allOnCaller && (std::this_thread::get_id() == callerId);
    });

    ASSERT_EQ(order.size(), 8u);
    for (uint32_t i = 0; i < 8; ++i)
    {
        EXPECT_EQ(order[i], i);
    }
    EXPECT_TRUE(allOnCaller);
}

TEST(JobSystemZeroWorkers, CurrentWorkerIndexIsZeroInsideJobs)
{
    ThreadPoolJobSystem jobs(0);
    jobs.ParallelFor(3, [&](uint32_t) {
        EXPECT_EQ(jobs.CurrentWorkerIndex(), 0u);
    });
}

TEST(JobSystemZeroWorkers, ZeroJobsReturnsWithoutInvokingCallback)
{
    ThreadPoolJobSystem jobs(0);
    jobs.ParallelFor(0, [&](uint32_t) { FAIL() << "callback ran for jobCount == 0"; });
}

TEST(JobSystemZeroWorkers, ReusableAcrossBatches)
{
    ThreadPoolJobSystem jobs(0);
    uint64_t sum = 0;
    for (uint32_t batch = 1; batch <= 50; ++batch)
    {
        jobs.ParallelFor(batch, [&](uint32_t i) { sum += i; });
    }
    // sum over batches of (0 + 1 + ... + batch-1)
    uint64_t expected = 0;
    for (uint32_t batch = 1; batch <= 50; ++batch)
    {
        expected += static_cast<uint64_t>(batch) * (batch - 1) / 2;
    }
    EXPECT_EQ(sum, expected);
}

//=============================================================================
// Threaded behavior. These are property tests: they cannot prove the absence
// of races (the TSan configuration covers that), but they catch lost jobs,
// double execution, and early-returning joins, which are the realistic
// failure modes of the wake/counter/join logic.
//=============================================================================

TEST(JobSystemThreaded, EveryIndexExecutesExactlyOnce)
{
    ThreadPoolJobSystem jobs(4);
    EXPECT_EQ(jobs.WorkerCount(), 4u);

    for (uint32_t jobCount : { 1u, 3u, 4u, 7u, 64u, 10000u })
    {
        std::vector<std::atomic<uint32_t>> hits(jobCount);
        jobs.ParallelFor(jobCount, [&](uint32_t i) { hits[i].fetch_add(1); });

        for (uint32_t i = 0; i < jobCount; ++i)
        {
            ASSERT_EQ(hits[i].load(), 1u) << "index " << i << " of " << jobCount;
        }
    }
}

TEST(JobSystemThreaded, JoinMakesPlainJobWritesVisibleToCaller)
{
    ThreadPoolJobSystem jobs(4);
    constexpr uint32_t JobCount = 4096;

    // Plain non-atomic writes: visibility after ParallelFor returns is part
    // of the join contract, not a property of the payload.
    std::vector<uint64_t> results(JobCount, 0);
    jobs.ParallelFor(JobCount, [&](uint32_t i) {
        results[i] = static_cast<uint64_t>(i) * i + 1;
    });

    for (uint32_t i = 0; i < JobCount; ++i)
    {
        ASSERT_EQ(results[i], static_cast<uint64_t>(i) * i + 1) << "index " << i;
    }
}

TEST(JobSystemThreaded, RepeatedBatchesStress)
{
    ThreadPoolJobSystem jobs(4);
    constexpr uint32_t Iterations = 300;
    constexpr uint32_t JobCount = 257;   // not a multiple of the worker count

    for (uint32_t iter = 0; iter < Iterations; ++iter)
    {
        std::atomic<uint64_t> sum{ 0 };
        jobs.ParallelFor(JobCount, [&](uint32_t i) { sum.fetch_add(i + 1); });
        ASSERT_EQ(sum.load(), static_cast<uint64_t>(JobCount) * (JobCount + 1) / 2)
            << "iteration " << iter;
    }
}

// Caller participation, proven deterministically: with W workers and W + 1
// jobs that all rendezvous on one latch, completion is impossible unless the
// calling thread executes a job too. A hang here means the caller stopped
// participating. Each participant runs exactly one job, so the collected
// worker indices must be exactly {0, 1, ..., W}.
TEST(JobSystemThreaded, CallerParticipatesAndWorkerIndicesAreDistinct)
{
    constexpr uint32_t Workers = 3;
    ThreadPoolJobSystem jobs(Workers);

    std::latch rendezvous(Workers + 1);
    std::mutex mutex;
    std::set<uint32_t> seenIndices;

    jobs.ParallelFor(Workers + 1, [&](uint32_t) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            seenIndices.insert(jobs.CurrentWorkerIndex());
        }
        rendezvous.arrive_and_wait();
    });

    ASSERT_EQ(seenIndices.size(), Workers + 1);
    for (uint32_t i = 0; i <= Workers; ++i)
    {
        EXPECT_TRUE(seenIndices.count(i)) << "worker index " << i << " never ran a job";
    }
}

TEST(JobSystemThreaded, CurrentWorkerIndexStaysWithinBounds)
{
    ThreadPoolJobSystem jobs(4);
    std::atomic<bool> inBounds{ true };

    jobs.ParallelFor(2048, [&](uint32_t) {
        if (jobs.CurrentWorkerIndex() > jobs.WorkerCount())
        {
            inBounds.store(false);
        }
    });

    EXPECT_TRUE(inBounds.load());
}

TEST(JobSystemThreaded, ZeroJobsReturnsWithoutInvokingCallback)
{
    ThreadPoolJobSystem jobs(4);
    std::atomic<uint32_t> calls{ 0 };
    jobs.ParallelFor(0, [&](uint32_t) { calls.fetch_add(1); });
    EXPECT_EQ(calls.load(), 0u);
}

TEST(JobSystemThreaded, PoolDestructionWithNoBatchesDoesNotHang)
{
    ThreadPoolJobSystem jobs(8);
    // Destructor runs at scope exit; the test passing is the assertion.
}

//=============================================================================
// Contract death tests. The contracts are debug asserts, matching the
// engine's existing EXPECT_DEATH pattern in test/ecs/EcsTests.cpp. The
// threadsafe death-test style is required because this binary's other tests
// spawn pool threads in the parent process.
//=============================================================================

TEST(JobSystemContracts, NestedParallelForDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ThreadPoolJobSystem jobs(0);
    EXPECT_DEATH(
        jobs.ParallelFor(1, [&](uint32_t) {
            jobs.ParallelFor(1, [](uint32_t) {});
        }),
        "no nesting");
}

TEST(JobSystemContracts, NestedParallelForAcrossPoolsDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ThreadPoolJobSystem outer(0);
    ThreadPoolJobSystem inner(0);
    EXPECT_DEATH(
        outer.ParallelFor(1, [&](uint32_t) {
            inner.ParallelFor(1, [](uint32_t) {});
        }),
        "no nesting");
}

TEST(JobSystemContracts, ConcurrentParallelForDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            ThreadPoolJobSystem jobs(1);
            std::latch jobStarted(1);
            std::latch releaseJob(1);

            // Holds a ParallelFor open: its single job blocks until released.
            std::thread holder([&] {
                jobs.ParallelFor(1, [&](uint32_t) {
                    jobStarted.count_down();
                    releaseJob.wait();
                });
            });

            jobStarted.wait();
            // Second ParallelFor while the first is in flight: must assert.
            jobs.ParallelFor(1, [](uint32_t) {});

            // Unreachable cleanup, present so the child exits if the assert
            // is ever compiled out.
            releaseJob.count_down();
            holder.join();
        },
        "single-active");
}

TEST(JobSystemContracts, CurrentWorkerIndexOutsideJobDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ThreadPoolJobSystem jobs(0);
    EXPECT_DEATH((void)jobs.CurrentWorkerIndex(), "only valid inside a job");
}

TEST(JobSystemContracts, ThrowingJobDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ThreadPoolJobSystem jobs(0);
    EXPECT_DEATH(
        jobs.ParallelFor(1, [](uint32_t) { throw 42; }),
        "job 0 threw");
}
