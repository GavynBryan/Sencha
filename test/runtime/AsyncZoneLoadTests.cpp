#include <gtest/gtest.h>

#include <jobs/AsyncTaskQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/ZoneRuntime.h>

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace
{
    struct ZoneLoadMarker
    {
        int Value = 0;
    };

    // The build callback the tests hand to the loader: exercises entity
    // creation and component registration on the detached registry — both
    // are safe off-thread because the registry is solely owned by the task.
    void BuildTestZone(Registry& registry, int entityCount)
    {
        registry.Components.RegisterComponent<ZoneLoadMarker>();
        for (int i = 0; i < entityCount; ++i)
        {
            const EntityId entity = registry.Entities.Create();
            registry.Components.AddComponent<ZoneLoadMarker>(entity, { i });
        }
    }
}

//=============================================================================
// Attach machinery (no loader): detached build on the calling thread.
//=============================================================================

TEST(ZoneRuntimeAttach, DetachedRegistryAttachesAndIsVisible)
{
    ZoneRuntime zones;
    const ZoneId zone{ 7 };

    const RegistryId reserved = zones.ReserveRegistryId();
    auto registry = std::make_unique<Registry>(MakeZoneRegistry(reserved, zone));
    BuildTestZone(*registry, 16);

    EXPECT_FALSE(zones.IsZoneLoaded(zone));
    Registry& attached = zones.AttachZone(std::move(registry), ZoneParticipation{ .Logic = true });

    EXPECT_TRUE(zones.IsZoneLoaded(zone));
    EXPECT_EQ(zones.FindZone(zone), &attached);
    EXPECT_EQ(zones.FindRegistry(reserved), &attached);
    EXPECT_EQ(attached.Entities.Count(), 16u);
    EXPECT_TRUE(zones.GetParticipation(zone).Logic);

    FrameRegistryView view = zones.BuildFrameView();
    ASSERT_EQ(view.Logic.size(), 1u);
    EXPECT_EQ(view.Logic[0], &attached);
}

TEST(ZoneRuntimeAttach, ReservedIdsNeverCollideWithCreateZone)
{
    ZoneRuntime zones;

    const RegistryId reserved = zones.ReserveRegistryId();
    Registry& created = zones.CreateZone(ZoneId{ 1 });
    EXPECT_NE(created.Id, reserved);

    auto registry = std::make_unique<Registry>(MakeZoneRegistry(reserved, ZoneId{ 2 }));
    Registry& attached = zones.AttachZone(std::move(registry));
    EXPECT_NE(attached.Id, created.Id);
    EXPECT_EQ(zones.ZoneCount(), 2u);
}

//=============================================================================
// AsyncZoneLoader, zero-thread queue: the deterministic end-to-end path.
//=============================================================================

TEST(AsyncZoneLoad, ZeroThreadEndToEnd)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId zone{ 3 };
    auto handle = loader.BeginLoad(
        zone,
        [](Registry& registry) { BuildTestZone(registry, 64); },
        ZoneParticipation{ .Visible = true, .Logic = true });

    EXPECT_TRUE(loader.IsLoading(zone));
    EXPECT_FALSE(zones.IsZoneLoaded(zone));

    // Work stage: builds the detached registry. Still not visible.
    EXPECT_EQ(tasks.PumpWork(), 1u);
    EXPECT_TRUE(loader.IsLoading(zone));
    EXPECT_FALSE(zones.IsZoneLoaded(zone));

    // Commit stage: attaches and marks the ZoneLoad discontinuity.
    EXPECT_EQ(tasks.DrainCompletions(), 1u);
    EXPECT_FALSE(loader.IsLoading(zone));
    EXPECT_TRUE(tasks.IsComplete(handle));
    ASSERT_TRUE(zones.IsZoneLoaded(zone));

    Registry* attached = zones.FindZone(zone);
    ASSERT_NE(attached, nullptr);
    EXPECT_EQ(attached->Entities.Count(), 64u);
    EXPECT_TRUE(zones.GetParticipation(zone).Visible);

    // Component data built off-frame is intact.
    int markers = 0;
    std::as_const(attached->Components).ForEachComponent<ZoneLoadMarker>(
        [&](EntityId, const ZoneLoadMarker& marker) { markers += (marker.Value >= 0) ? 1 : 0; });
    EXPECT_EQ(markers, 64);

    EXPECT_EQ(runtime.GetCurrentFrame().DiscontinuityReason,
              TemporalDiscontinuityReason::ZoneLoad);
}

TEST(AsyncZoneLoad, FinalizeRunsOnOwnerThreadAfterAttachBeforeDiscontinuity)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId zone{ 8 };
    bool finalized = false;

    loader.BeginLoad(
        zone,
        [](Registry& registry) { BuildTestZone(registry, 5); },
        [&](Registry& registry) {
            finalized = true;
            // The zone is already attached and queryable when finalize runs...
            EXPECT_TRUE(zones.IsZoneLoaded(zone));
            EXPECT_EQ(zones.FindZone(zone), &registry);
            EXPECT_EQ(registry.Entities.Count(), 5u);
            // ...but the discontinuity has not been marked yet.
            EXPECT_NE(runtime.GetCurrentFrame().DiscontinuityReason,
                      TemporalDiscontinuityReason::ZoneLoad);
            // Finalize is the main-thread publish step: ambient state and
            // further registry mutation are both legal here.
            registry.Entities.Create();
        },
        // Participating attach: the ZoneLoad discontinuity fires after finalize.
        ZoneParticipation{ .Logic = true });

    tasks.PumpWork();
    EXPECT_FALSE(finalized);  // finalize belongs to commit, not work

    tasks.DrainCompletions();
    EXPECT_TRUE(finalized);
    EXPECT_EQ(zones.FindZone(zone)->Entities.Count(), 6u);
    EXPECT_EQ(runtime.GetCurrentFrame().DiscontinuityReason,
              TemporalDiscontinuityReason::ZoneLoad);
}

// The genre-critical path: preloading the next room while the current one
// plays must not reset presentation history. A dormant attach (default
// participation) is invisible to every frame span, so no discontinuity is
// marked; activation is a later, separate game decision.
TEST(AsyncZoneLoad, DormantPreloadAttachesSeamlessly)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId nextRoom{ 11 };
    loader.BeginLoad(nextRoom, [](Registry& registry) { BuildTestZone(registry, 12); });

    tasks.PumpWork();
    tasks.DrainCompletions();

    // Attached and queryable, but seamless: no discontinuity, and the zone
    // appears in no frame span.
    ASSERT_TRUE(zones.IsZoneLoaded(nextRoom));
    EXPECT_EQ(runtime.GetCurrentFrame().DiscontinuityReason,
              TemporalDiscontinuityReason::None);
    FrameRegistryView dormantView = zones.BuildFrameView();
    EXPECT_TRUE(dormantView.Visible.empty());
    EXPECT_TRUE(dormantView.Logic.empty());

    // The player crosses the doorway: the game flips the room live.
    zones.SetParticipation(nextRoom, ZoneParticipation{ .Visible = true, .Logic = true });
    FrameRegistryView liveView = zones.BuildFrameView();
    ASSERT_EQ(liveView.Logic.size(), 1u);
    EXPECT_EQ(liveView.Logic[0], zones.FindZone(nextRoom));
}

TEST(AsyncZoneLoad, CancelBeforeWorkDropsTheLoad)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId zone{ 4 };
    bool buildRan = false;
    loader.BeginLoad(zone, [&](Registry&) { buildRan = true; });

    EXPECT_TRUE(loader.CancelLoad(zone));
    EXPECT_FALSE(loader.IsLoading(zone));
    EXPECT_EQ(tasks.PumpWork(), 0u);
    EXPECT_EQ(tasks.DrainCompletions(), 0u);
    EXPECT_FALSE(buildRan);
    EXPECT_FALSE(zones.IsZoneLoaded(zone));
}

TEST(AsyncZoneLoad, CancelAfterWorkDropsTheAttach)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId zone{ 5 };
    loader.BeginLoad(zone, [](Registry& registry) { BuildTestZone(registry, 8); });

    EXPECT_EQ(tasks.PumpWork(), 1u);
    EXPECT_TRUE(loader.CancelLoad(zone));
    EXPECT_EQ(tasks.DrainCompletions(), 0u);
    EXPECT_FALSE(zones.IsZoneLoaded(zone));
    EXPECT_FALSE(loader.IsLoading(zone));
}

TEST(AsyncZoneLoad, ReloadAfterDestroyUsesAFreshRegistry)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId zone{ 6 };
    loader.BeginLoad(zone, [](Registry& r) { BuildTestZone(r, 4); });
    tasks.PumpWork();
    tasks.DrainCompletions();
    ASSERT_TRUE(zones.IsZoneLoaded(zone));

    EXPECT_TRUE(zones.DestroyZone(zone));
    loader.BeginLoad(zone, [](Registry& r) { BuildTestZone(r, 9); });
    tasks.PumpWork();
    tasks.DrainCompletions();
    ASSERT_TRUE(zones.IsZoneLoaded(zone));
    EXPECT_EQ(zones.FindZone(zone)->Entities.Count(), 9u);
}

//=============================================================================
// Threaded smoke test: the build genuinely runs off the calling thread while
// the caller keeps polling, mirroring how a frame would.
//=============================================================================

TEST(AsyncZoneLoad, ThreadedLoadCompletesViaPolling)
{
    AsyncTaskQueue tasks(1);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    const ZoneId zone{ 9 };
    std::thread::id buildThread;
    loader.BeginLoad(
        zone,
        [&](Registry& registry) {
            buildThread = std::this_thread::get_id();
            BuildTestZone(registry, 32);
        },
        ZoneParticipation{ .Visible = true });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!zones.IsZoneLoaded(zone))
    {
        tasks.DrainCompletions();
        ASSERT_LT(std::chrono::steady_clock::now(), deadline) << "zone load timed out";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_NE(buildThread, std::this_thread::get_id());
    EXPECT_FALSE(loader.IsLoading(zone));
    EXPECT_EQ(zones.FindZone(zone)->Entities.Count(), 32u);
    EXPECT_EQ(runtime.GetCurrentFrame().DiscontinuityReason,
              TemporalDiscontinuityReason::ZoneLoad);
}

//=============================================================================
// Contract death tests.
//=============================================================================

TEST(AsyncZoneLoadContracts, BeginLoadOnLoadedZoneDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    zones.CreateZone(ZoneId{ 2 });
    EXPECT_DEATH(loader.BeginLoad(ZoneId{ 2 }, [](Registry&) {}), "already loaded");
}

TEST(AsyncZoneLoadContracts, BeginLoadWhileInFlightDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    loader.BeginLoad(ZoneId{ 2 }, [](Registry&) {});
    EXPECT_DEATH(loader.BeginLoad(ZoneId{ 2 }, [](Registry&) {}), "already in flight");
}

TEST(AsyncZoneLoadContracts, AttachingDuplicateZoneDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ZoneRuntime zones;
    zones.CreateZone(ZoneId{ 3 });

    auto duplicate = std::make_unique<Registry>(
        MakeZoneRegistry(zones.ReserveRegistryId(), ZoneId{ 3 }));
    EXPECT_DEATH(zones.AttachZone(std::move(duplicate)), "duplicate zone");
}
