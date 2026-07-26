// Job-system overhead benchmark plus unified-world partition-filter scaling.
// Run with an optimized build; debug numbers mostly measure assertions.

#include <ecs/WorldComponentSchema.h>
#include <jobs/ThreadPoolJobSystem.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

uint64_t SpinWork(uint64_t seed, uint32_t iterations)
{
    uint64_t value = seed | 1;
    for (uint32_t index = 0; index < iterations; ++index)
    {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    return value;
}

volatile uint64_t g_sink = 0;

double MedianRunUs(
    ThreadPoolJobSystem& jobs,
    uint32_t jobCount,
    uint32_t workIterations,
    int repetitions)
{
    std::vector<double> samples;
    samples.reserve(repetitions);
    for (int repetition = 0; repetition < repetitions; ++repetition)
    {
        const auto start = Clock::now();
        jobs.ParallelFor(jobCount, [&](uint32_t index)
        {
            g_sink = g_sink + SpinWork(index, workIterations);
        });
        const auto end = Clock::now();
        samples.push_back(
            std::chrono::duration<double, std::micro>(end - start)
                .count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

WorldComponentSchema TransformSchema()
{
    WorldComponentSchema schema;
    schema.Add<LocalTransform>();
    schema.Add<WorldTransform>();
    schema.Add<Parent>();
    schema.Seal();
    return schema;
}

EntityId CreateTransformEntity(
    World& world,
    StoragePartitionId partition,
    const Transform3f& transform)
{
    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    world.AddComponent<WorldTransform>(
        entity,
        WorldTransform{ transform });
    return entity;
}

void PopulatePartition(
    World& world,
    StoragePartitionId partition,
    uint32_t entityCount,
    std::vector<EntityId>& roots)
{
    constexpr uint32_t childrenPerRoot = 9;
    uint32_t created = 0;
    while (created < entityCount)
    {
        const float x = static_cast<float>(created);
        const EntityId root = CreateTransformEntity(
            world,
            partition,
            Transform3f(
                Vec3d(x, 0.0f, 0.0f),
                Quatf::Identity(),
                Vec3d::One()));
        roots.push_back(root);
        ++created;

        for (uint32_t childIndex = 0;
             childIndex < childrenPerRoot && created < entityCount;
             ++childIndex, ++created)
        {
            const EntityId child = CreateTransformEntity(
                world,
                partition,
                Transform3f(
                    Vec3d(0.0f, 1.0f, 0.0f),
                    Quatf::Identity(),
                    Vec3d::One()));
            world.AddComponent<Parent>(child, Parent{ root });
        }
    }
}

void DirtyRoots(World& world, const std::vector<EntityId>& roots)
{
    for (EntityId root : roots)
    {
        if (LocalTransform* local =
                world.TryGet<LocalTransform>(root))
        {
            local->Value.Position.X += 0.001f;
        }
    }
}

void RunPartitionPropagationScenario()
{
    constexpr uint32_t entitiesPerZone = 10000;
    constexpr int repetitions = 15;

    std::printf(
        "Unified-world transform propagation (%u entities/zone):\n",
        entitiesPerZone);
    std::printf(
        "%10s | %14s\n",
        "active-zones",
        "propagate-ms");

    for (uint32_t zoneCount : { 1u, 2u, 4u, 8u })
    {
        const WorldComponentSchema schema = TransformSchema();
        RuntimeWorld runtime(schema);
        std::vector<std::vector<EntityId>> roots(zoneCount);

        for (uint32_t index = 0; index < zoneCount; ++index)
        {
            RuntimeZoneRecord& zone = runtime.AttachZone(
                ZoneId{ static_cast<uint16_t>(index + 1) },
                ZoneParticipation{ .Logic = true });
            PopulatePartition(
                runtime.Entities(),
                zone.Partition,
                entitiesPerZone,
                roots[index]);
        }
        (void)runtime.BeginResidencyProcessing();
        runtime.FinalizeResidencyProcessing();
        const FrameZoneView& view = runtime.BuildFrameView();

        std::vector<double> samples;
        samples.reserve(repetitions);
        for (int repetition = 0;
             repetition < repetitions;
             ++repetition)
        {
            for (const auto& zoneRoots : roots)
                DirtyRoots(runtime.Entities(), zoneRoots);

            const auto start = Clock::now();
            PropagateTransforms(
                runtime.Entities(),
                view.Logic,
                TransformPropagationDomain::Simulation);
            const auto end = Clock::now();
            samples.push_back(
                std::chrono::duration<double, std::milli>(end - start)
                    .count());
        }
        std::sort(samples.begin(), samples.end());
        std::printf(
            "%10u | %14.3f\n",
            zoneCount,
            samples[samples.size() / 2]);

        runtime.EndFrameView();
    }
    std::printf("\n");
}
} // namespace

int main()
{
    const uint32_t hardwareThreads =
        std::thread::hardware_concurrency();
    const uint32_t pooledWorkers =
        hardwareThreads > 2 ? hardwareThreads - 2 : 1;

    std::printf(
        "hardware_concurrency = %u, pooled workers = %u\n\n",
        hardwareThreads,
        pooledWorkers);

    constexpr int repetitions = 51;
    const uint32_t jobCounts[] = { 1, 8, 64, 512, 4096 };
    const uint32_t workIterations[] = { 0, 100, 1000, 10000 };

    ThreadPoolJobSystem serial(0);
    ThreadPoolJobSystem pool(pooledWorkers);

    std::printf(
        "%10s %12s | %12s %12s %10s\n",
        "jobs",
        "work-iters",
        "serial-us",
        "pool-us",
        "speedup");

    for (uint32_t work : workIterations)
    {
        for (uint32_t count : jobCounts)
        {
            const double serialUs =
                MedianRunUs(serial, count, work, repetitions);
            const double poolUs =
                MedianRunUs(pool, count, work, repetitions);
            std::printf(
                "%10u %12u | %12.2f %12.2f %9.2fx\n",
                count,
                work,
                serialUs,
                poolUs,
                serialUs / poolUs);
        }
        std::printf("\n");
    }

    std::printf(
        "Per-dispatch overhead floor: pool-us at jobs=1, work-iters=0.\n\n");

    RunPartitionPropagationScenario();
    return 0;
}
