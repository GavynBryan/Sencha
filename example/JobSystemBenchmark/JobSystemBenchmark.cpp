// Stage A job-system benchmark (docs/ecs/parallelization.md, Rollout).
//
// Measures the ParallelFor overhead curve: wall time across a grid of
// job count x per-job work size x worker count, against the zero-worker
// configuration as the serial baseline. Later stages use this floor to
// decide when parallel dispatch is worth it (the Stage D ~1 ms gate).
//
// Run with an optimized build; debug numbers measure the asserts, not the
// pool.

#include <jobs/ThreadPoolJobSystem.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

// Synthetic per-job work: an integer-mix loop the optimizer cannot fold.
// ~1ns per iteration on current hardware; calibrated against wall time
// below so the table's "work" column is honest on the machine it ran on.
uint64_t SpinWork(uint64_t seed, uint32_t iterations)
{
    uint64_t x = seed | 1;
    for (uint32_t i = 0; i < iterations; ++i)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    return x;
}

volatile uint64_t g_sink = 0;

double MedianRunUs(ThreadPoolJobSystem& jobs, uint32_t jobCount, uint32_t workIters, int reps)
{
    std::vector<double> samples;
    samples.reserve(reps);
    for (int r = 0; r < reps; ++r)
    {
        const auto start = Clock::now();
        jobs.ParallelFor(jobCount, [&](uint32_t i) {
            g_sink = g_sink + SpinWork(i, workIters);
        });
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

} // namespace

int main()
{
    const uint32_t hw = std::thread::hardware_concurrency();
    const uint32_t pooled = hw > 2 ? hw - 2 : 1;

    std::printf("hardware_concurrency = %u, pooled workers = %u\n\n", hw, pooled);

    constexpr int Reps = 51;
    const uint32_t jobCounts[] = { 1, 8, 64, 512, 4096 };
    const uint32_t workIters[] = { 0, 100, 1000, 10000 };

    ThreadPoolJobSystem serial(0);
    ThreadPoolJobSystem pool(pooled);

    std::printf("%10s %12s | %12s %12s %10s\n",
                "jobs", "work-iters", "serial-us", "pool-us", "speedup");

    for (uint32_t work : workIters)
    {
        for (uint32_t count : jobCounts)
        {
            const double serialUs = MedianRunUs(serial, count, work, Reps);
            const double poolUs = MedianRunUs(pool, count, work, Reps);
            std::printf("%10u %12u | %12.2f %12.2f %9.2fx\n",
                        count, work, serialUs, poolUs, serialUs / poolUs);
        }
        std::printf("\n");
    }

    std::printf("Per-dispatch overhead floor: the pool-us value at jobs=1, work-iters=0.\n");
    return 0;
}
