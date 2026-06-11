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
#include <math/geometry/3d/Transform3d.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>
#include <zone/DefaultZoneBuilder.h>
#include <zone/ZoneRuntime.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
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

// ── Stage C scenario: zone-parallel transform propagation ───────────────────
// One job per zone; each zone holds entityCount entities as 10-wide parented
// trees. Roots are dirtied before every measured run so propagation does full
// work instead of the dirty-subtree fast path.

void PopulateZone(Registry& registry, uint32_t entityCount, std::vector<EntityId>& roots)
{
    constexpr uint32_t ChildrenPerRoot = 9;
    uint32_t created = 0;
    while (created < entityCount)
    {
        const float x = static_cast<float>(created);
        EntityId root = CreateDefaultEntity(
            registry, Transform3f(Vec3d(x, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
        roots.push_back(root);
        ++created;
        for (uint32_t c = 0; c < ChildrenPerRoot && created < entityCount; ++c, ++created)
        {
            EntityId child = CreateDefaultEntity(
                registry, Transform3f(Vec3d(0.0f, 1.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
            registry.Components.AddComponent(child, Parent{ root });
        }
    }
}

void DirtyRoots(Registry& registry, const std::vector<EntityId>& roots)
{
    for (EntityId root : roots)
    {
        if (LocalTransform* local = registry.Components.TryGet<LocalTransform>(root))
        {
            local->Value.Position.X += 0.001f;
        }
    }
}

void RunZonePropagationScenario(uint32_t pooledWorkers)
{
    constexpr uint32_t EntitiesPerZone = 10000;
    constexpr int Reps = 15;

    std::printf("Zone-parallel transform propagation (%u entities/zone, %u workers):\n",
                EntitiesPerZone, pooledWorkers);
    std::printf("%10s | %12s %12s %10s\n", "zones", "serial-ms", "parallel-ms", "speedup");

    ThreadPoolJobSystem pool(pooledWorkers);

    for (uint32_t zoneCount : { 1u, 2u, 4u, 8u })
    {
        ZoneRuntime zones;
        std::vector<std::vector<EntityId>> roots(zoneCount);
        for (uint32_t i = 0; i < zoneCount; ++i)
        {
            Registry& registry = CreateDefault3DZone(
                zones, ZoneId{ static_cast<uint16_t>(i + 1) }, ZoneParticipation{ .Logic = true });
            PopulateZone(registry, EntitiesPerZone, roots[i]);
        }
        FrameRegistryView view = zones.BuildFrameView();

        auto measure = [&](auto&& propagate) {
            std::vector<double> samples;
            samples.reserve(Reps);
            for (int rep = 0; rep < Reps; ++rep)
            {
                for (uint32_t i = 0; i < zoneCount; ++i)
                {
                    DirtyRoots(*view.Logic[i], roots[i]);
                }
                const auto start = Clock::now();
                propagate();
                const auto end = Clock::now();
                samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            }
            std::sort(samples.begin(), samples.end());
            return samples[samples.size() / 2];
        };

        const double serialMs = measure([&] { PropagateTransforms(view.Logic); });
        const double parallelMs = measure([&] { PropagateTransforms(pool, view.Logic); });

        std::printf("%10u | %12.3f %12.3f %9.2fx\n",
                    zoneCount, serialMs, parallelMs, serialMs / parallelMs);
    }
    std::printf("\n");
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

    std::printf("Per-dispatch overhead floor: the pool-us value at jobs=1, work-iters=0.\n\n");

    RunZonePropagationScenario(pooled);
    return 0;
}
