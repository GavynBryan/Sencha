#include <gtest/gtest.h>

#include <jobs/ThreadPoolJobSystem.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/registry/Registry.h>
#include <world/registry/RegistryParallel.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>
#include <zone/DefaultZoneBuilder.h>
#include <zone/ZoneRuntime.h>

#include <array>
#include <latch>
#include <set>
#include <thread>
#include <vector>

namespace
{
    // Deterministic multi-zone content: every zone gets a three-deep parented
    // chain plus a handful of independent roots, all positioned from indices
    // so two builds of the same shape are exactly identical.
    void BuildHierarchyZone(Registry& registry, uint32_t zoneSeed)
    {
        const float base = static_cast<float>(zoneSeed) * 100.0f;

        EntityId root = CreateDefaultEntity(
            registry, Transform3f(Vec3d(base, 1.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
        EntityId child = CreateDefaultEntity(
            registry, Transform3f(Vec3d(0.0f, 2.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
        EntityId grandchild = CreateDefaultEntity(
            registry, Transform3f(Vec3d(0.0f, 0.0f, 3.0f), Quatf::Identity(), Vec3d::One()));
        registry.Components.AddComponent(child, Parent{ root });
        registry.Components.AddComponent(grandchild, Parent{ child });

        for (uint32_t i = 0; i < 8; ++i)
        {
            CreateDefaultEntity(
                registry,
                Transform3f(Vec3d(base + static_cast<float>(i), 0.0f, -1.0f),
                            Quatf::Identity(), Vec3d::One()));
        }
    }

    void BuildZones(ZoneRuntime& zones, uint32_t zoneCount)
    {
        for (uint32_t i = 0; i < zoneCount; ++i)
        {
            Registry& registry = CreateDefault3DZone(
                zones, ZoneId{ static_cast<uint16_t>(i + 1) }, ZoneParticipation{ .Logic = true });
            BuildHierarchyZone(registry, i);
        }
    }

    void ExpectIdenticalWorldTransforms(ZoneRuntime& a, ZoneRuntime& b, uint32_t zoneCount)
    {
        for (uint32_t i = 0; i < zoneCount; ++i)
        {
            const ZoneId zone{ static_cast<uint16_t>(i + 1) };
            Registry* ra = a.FindZone(zone);
            Registry* rb = b.FindZone(zone);
            ASSERT_NE(ra, nullptr);
            ASSERT_NE(rb, nullptr);

            const auto entities = ra->Entities.GetAliveEntities();
            ASSERT_EQ(entities.size(), rb->Entities.GetAliveEntities().size());
            for (EntityId entity : entities)
            {
                const WorldTransform* wa = std::as_const(ra->Components).TryGet<WorldTransform>(entity);
                const WorldTransform* wb = std::as_const(rb->Components).TryGet<WorldTransform>(entity);
                ASSERT_NE(wa, nullptr);
                ASSERT_NE(wb, nullptr);
                EXPECT_EQ(wa->Value.Position, wb->Value.Position)
                    << "zone " << i << " entity " << entity.Index;
            }
        }
    }
}

//=============================================================================
// ForEachRegistryParallel: the Decision 4 helper.
//=============================================================================

TEST(ForEachRegistryParallel, VisitsEveryRegistryExactlyOnce)
{
    ThreadPoolJobSystem jobs(4);
    std::array<Registry, 5> storage;
    std::array<Registry*, 5> registries;
    for (size_t i = 0; i < storage.size(); ++i)
    {
        registries[i] = &storage[i];
    }

    std::array<std::atomic<int>, 5> visits{};
    ForEachRegistryParallel(jobs, std::span<Registry* const>(registries),
                            [&](Registry& registry) {
                                for (size_t i = 0; i < storage.size(); ++i)
                                {
                                    if (&registry == &storage[i])
                                    {
                                        visits[i].fetch_add(1);
                                    }
                                }
                            });

    for (size_t i = 0; i < visits.size(); ++i)
    {
        EXPECT_EQ(visits[i].load(), 1) << "registry " << i;
    }
}

TEST(ForEachRegistryParallel, SingleEntryRunsInlineOnCaller)
{
    ThreadPoolJobSystem jobs(4);
    Registry registry;
    std::array<Registry*, 1> registries{ &registry };

    std::thread::id ranOn;
    ForEachRegistryParallel(jobs, std::span<Registry* const>(registries),
                            [&](Registry&) { ranOn = std::this_thread::get_id(); });

    // No fork for one registry: the dispatch floor is never paid for a
    // workload that cannot parallelize.
    EXPECT_EQ(ranOn, std::this_thread::get_id());
}

TEST(ForEachRegistryParallel, EmptySpanAndNullEntriesAreSkipped)
{
    ThreadPoolJobSystem jobs(2);
    int calls = 0;

    ForEachRegistryParallel(jobs, std::span<Registry* const>(),
                            [&](Registry&) { ++calls; });
    EXPECT_EQ(calls, 0);

    Registry registry;
    std::array<Registry*, 3> withNulls{ nullptr, &registry, nullptr };
    std::atomic<int> parallelCalls{ 0 };
    ForEachRegistryParallel(jobs, std::span<Registry* const>(withNulls),
                            [&](Registry&) { parallelCalls.fetch_add(1); });
    EXPECT_EQ(parallelCalls.load(), 1);
}

// With W workers and W + 1 registries rendezvousing on a latch, completion
// requires the registries to genuinely run concurrently (and the caller to
// participate). A hang here means the helper stopped forking.
TEST(ForEachRegistryParallel, RegistriesRunConcurrently)
{
    constexpr uint32_t Workers = 3;
    ThreadPoolJobSystem jobs(Workers);

    std::array<Registry, Workers + 1> storage;
    std::array<Registry*, Workers + 1> registries;
    for (size_t i = 0; i < storage.size(); ++i)
    {
        registries[i] = &storage[i];
    }

    std::latch rendezvous(Workers + 1);
    ForEachRegistryParallel(jobs, std::span<Registry* const>(registries),
                            [&](Registry&) { rendezvous.arrive_and_wait(); });
}

//=============================================================================
// Zone-parallel transform propagation: the Stage C success criterion is
// bit-identical results against the single-threaded configuration.
//=============================================================================

TEST(ZoneParallelPropagation, MatchesSingleThreadedConfigurationExactly)
{
    constexpr uint32_t ZoneCount = 6;

    ZoneRuntime serialZones;
    ZoneRuntime parallelZones;
    BuildZones(serialZones, ZoneCount);
    BuildZones(parallelZones, ZoneCount);

    ThreadPoolJobSystem serialJobs(0);
    ThreadPoolJobSystem parallelJobs(4);

    PropagateTransforms(serialJobs, serialZones.BuildFrameView().Logic);
    PropagateTransforms(parallelJobs, parallelZones.BuildFrameView().Logic);

    ExpectIdenticalWorldTransforms(serialZones, parallelZones, ZoneCount);
}

TEST(ZoneParallelPropagation, MatchesTheSerialOverloadExactly)
{
    constexpr uint32_t ZoneCount = 4;

    ZoneRuntime serialZones;
    ZoneRuntime parallelZones;
    BuildZones(serialZones, ZoneCount);
    BuildZones(parallelZones, ZoneCount);

    ThreadPoolJobSystem jobs(4);

    PropagateTransforms(serialZones.BuildFrameView().Logic);
    PropagateTransforms(jobs, parallelZones.BuildFrameView().Logic);

    ExpectIdenticalWorldTransforms(serialZones, parallelZones, ZoneCount);
}

TEST(ZoneParallelPropagation, ChildChainsResolveThroughParents)
{
    ZoneRuntime zones;
    BuildZones(zones, 3);
    ThreadPoolJobSystem jobs(4);

    PropagateTransforms(jobs, zones.BuildFrameView().Logic);

    for (uint32_t i = 0; i < 3; ++i)
    {
        Registry* registry = zones.FindZone(ZoneId{ static_cast<uint16_t>(i + 1) });
        ASSERT_NE(registry, nullptr);
        const auto entities = registry->Entities.GetAliveEntities();
        ASSERT_GE(entities.size(), 3u);

        const float base = static_cast<float>(i) * 100.0f;
        const WorldTransform* grandchild =
            std::as_const(registry->Components).TryGet<WorldTransform>(entities[2]);
        ASSERT_NE(grandchild, nullptr);
        // root(base,1,0) + child(0,2,0) + grandchild(0,0,3), identity rotations.
        EXPECT_EQ(grandchild->Value.Position, Vec3d(base, 3.0f, 3.0f));
    }
}

TEST(ZoneParallelPropagation, DuplicateSpanEntriesPropagateOnce)
{
    ZoneRuntime zones;
    BuildZones(zones, 1);
    Registry* registry = zones.FindZone(ZoneId{ 1 });
    ASSERT_NE(registry, nullptr);

    ThreadPoolJobSystem jobs(4);
    std::array<Registry*, 4> duplicated{ registry, registry, registry, registry };

    // The overload deduplicates before forking; duplicates would otherwise
    // race one World across jobs.
    PropagateTransforms(jobs, duplicated);

    const auto entities = registry->Entities.GetAliveEntities();
    const WorldTransform* root =
        std::as_const(registry->Components).TryGet<WorldTransform>(entities[0]);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->Value.Position, Vec3d(0.0f, 1.0f, 0.0f));
}
