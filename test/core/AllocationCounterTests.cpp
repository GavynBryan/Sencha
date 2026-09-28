// The counter sees every form of allocation, so a test that finds none has not
// simply looked past one.

#include "AllocationCounter.h"

#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
    struct alignas(64) Wide
    {
        char Bytes[64];
    };
}

TEST(AllocationCounter, CountsEveryFormOfNew)
{
    const std::size_t before = AllocationCount();
    delete new int(1);
    delete[] new int[4];
    delete new (std::nothrow) int(2);
    delete[] new (std::nothrow) int[4];
    delete new Wide;
    delete[] new Wide[2];
    EXPECT_EQ(AllocationCount() - before, 6u);
}

TEST(AllocationCounter, CountsAllocationsOnWorkers)
{
    JobSystem jobs(4);
    jobs.ParallelFor(4, [](uint32_t) {});
    const std::size_t before = AllocationCount();
    jobs.ParallelFor(400, [](uint32_t index) { delete new uint32_t(index); });
    EXPECT_GE(AllocationCount() - before, 400u);
}
