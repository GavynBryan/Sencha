// Teardown and detach-window behavior of the async zone lane.
//
// Two questions the existing suites do not ask. First, what happens when the
// loader, the world, and the task queue are destroyed while a load is still in
// flight — the shape a game gets when it resets its partition, or exits, with
// zones streaming. Second, what a zone looks like during the window between
// being marked for detach and the partition actually going away, because the
// streaming policy decides what to load from residency alone and that window is
// the one moment when "not resident" does not mean "not there".
//
// The destruction tests assert reachability rather than a value: they exist to
// be run under a sanitizer, where a commit or continuation firing against freed
// state is the failure. Under an ordinary build they only prove no crash.

#include <gtest/gtest.h>

#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <world/build/EntityBuildPackage.h>

#include <memory>
#include <optional>

namespace
{
struct LifetimeMarker
{
    int Value = 0;
};
}  // namespace
SENCHA_DECLARE_COMPONENT_TYPE(LifetimeMarker, "test.zone_lifetime_marker");

namespace
{
WorldComponentSchema LifetimeSchema()
{
    WorldComponentSchema schema;
    schema.Add<LifetimeMarker>();
    schema.Seal();
    return schema;
}

void BuildLifetimeZone(EntityBuildPackage& package, int entities)
{
    for (int index = 0; index < entities; ++index)
    {
        const PackageEntityId entity = package.CreateEntity();
        (void)package.AddComponent(entity, LifetimeMarker{ index });
    }
}

// Member order mirrors every other construction site: the queue outlives the
// loader, and the loader outlives nothing it holds a reference to. The order is
// the only thing keeping an in-flight commit from running against freed state,
// which is what these tests are here to exercise.
struct LoaderHarness
{
    explicit LoaderHarness(unsigned workers)
        : Tasks(workers)
        , Schema(LifetimeSchema())
        , World(Schema)
        , SceneContext(Logging)
        , Loader(Tasks, World, Schema, Serializers, SceneContext, FrameLoop)
    {
    }

    AsyncTaskQueue Tasks;
    WorldComponentSchema Schema;
    RuntimeWorld World;
    ComponentSerializerRegistry Serializers;
    LoggingProvider Logging;
    SceneSerializationContext SceneContext;
    RuntimeFrameLoop FrameLoop;
    AsyncZoneLoader Loader;
};

void ProcessResidency(RuntimeWorld& world)
{
    (void)world.BeginResidencyProcessing();
    world.FinalizeResidencyProcessing();
}

// Drives one zone all the way to resident on the serial lane.
void LoadZoneToResident(LoaderHarness& harness, ZoneId zone, int entities = 8)
{
    harness.Loader.BeginLoad(
        zone,
        [entities](EntityBuildPackage& package) { BuildLifetimeZone(package, entities); },
        ZoneParticipation{ .Visible = true, .Logic = true });
    harness.Tasks.PumpWork();
    harness.Tasks.DrainCompletions();
    harness.World.FlushLifecycleRequests();
    ProcessResidency(harness.World);
}
}  // namespace

// A queued build that is never pumped: the task holds the build callback, and
// the commit continuation holds the loader. Destroying the whole harness must
// not run either against freed state.
TEST(AsyncZoneLoaderLifetime, DestroyingTheHarnessWithAQueuedLoadIsSafe)
{
    bool built = false;
    {
        LoaderHarness harness(0);
        harness.Loader.BeginLoad(
            ZoneId{ 0x51 },
            [&built](EntityBuildPackage& package)
            {
                built = true;
                BuildLifetimeZone(package, 32);
            },
            ZoneParticipation{ .Visible = true });

        ASSERT_TRUE(harness.Loader.IsLoading(ZoneId{ 0x51 }));
        // Deliberately no pump and no drain: the work and its commit are still
        // owned by the queue when everything below it is destroyed.
    }
    EXPECT_FALSE(built) << "an unpumped build ran anyway";
}

// The work stage has run and its result is sitting in the completion queue,
// waiting for a drain that never comes. The commit captures the loader; this is
// the shape that decides whether teardown ordering is load-bearing.
TEST(AsyncZoneLoaderLifetime, DestroyingTheHarnessWithAnUndrainedCommitIsSafe)
{
    bool committed = false;
    {
        LoaderHarness harness(0);
        harness.Loader.BeginLoad(
            ZoneId{ 0x52 },
            [](EntityBuildPackage& package) { BuildLifetimeZone(package, 32); },
            [&committed](RuntimeWorld&, RuntimeZoneRecord&)
            {
                committed = true;
                return true;
            },
            ZoneParticipation{ .Visible = true });

        harness.Tasks.PumpWork();
        ASSERT_TRUE(harness.Loader.IsLoading(ZoneId{ 0x52 }));
        // No DrainCompletions: the commit is queued against a loader that is
        // about to be destroyed.
    }
    EXPECT_FALSE(committed) << "an undrained commit ran during teardown";
}

// The same teardown with worker threads, where the build may still be executing
// when destruction begins. The queue joins its workers; the question is whether
// anything it joins can touch the world that is being destroyed alongside it.
TEST(AsyncZoneLoaderLifetime, DestroyingTheHarnessWithThreadedWorkInFlightIsSafe)
{
    {
        LoaderHarness harness(4);
        for (std::uint64_t index = 0; index < 8; ++index)
        {
            harness.Loader.BeginLoad(
                ZoneId{ 0x60 + index },
                [](EntityBuildPackage& package) { BuildLifetimeZone(package, 512); },
                ZoneParticipation{ .Visible = true, .Logic = true });
        }
        // No settle: destruction races the workers on purpose.
    }
    SUCCEED() << "teardown with threaded loads in flight completed";
}

// A game resetting its world drops the partition state and rebuilds it. The
// second harness must come up clean rather than inheriting anything from the
// first one's unfinished work.
TEST(AsyncZoneLoaderLifetime, AHarnessRebuiltAfterAnAbandonedLoadStartsClean)
{
    {
        LoaderHarness first(0);
        first.Loader.BeginLoad(
            ZoneId{ 0x70 },
            [](EntityBuildPackage& package) { BuildLifetimeZone(package, 64); },
            ZoneParticipation{ .Visible = true });
        first.Tasks.PumpWork();
    }

    LoaderHarness second(0);
    EXPECT_FALSE(second.Loader.IsLoading(ZoneId{ 0x70 }));
    EXPECT_FALSE(second.World.IsZoneResident(ZoneId{ 0x70 }));

    LoadZoneToResident(second, ZoneId{ 0x70 });
    EXPECT_TRUE(second.World.IsZoneResident(ZoneId{ 0x70 }));
}

// The detach window, stated as the invariant a caller has to know about: once a
// detach is flushed, the zone stops reporting as resident while its record is
// still very much alive. Anything that decides "can I load this zone" from
// residency alone is, in this window, wrong.
TEST(AsyncZoneLoaderLifetime, ADetachingZoneStopsBeingResidentBeforeItsRecordIsGone)
{
    LoaderHarness harness(0);
    const ZoneId zone{ 0x80 };
    LoadZoneToResident(harness, zone);
    ASSERT_TRUE(harness.World.IsZoneResident(zone));

    harness.Loader.RequestDestroy(zone);
    // The destroy request is itself an async task: its commit is what marks the
    // zone for detach and flushes it, so the work stage has to run first.
    harness.Tasks.PumpWork();
    harness.Tasks.DrainCompletions();

    // The window: flushed, not yet finalized.
    EXPECT_FALSE(harness.World.IsZoneResident(zone))
        << "a detaching zone still reported as resident";
    EXPECT_NE(harness.World.FindZone(zone), nullptr)
        << "the record was gone before residency processing ran";

    ProcessResidency(harness.World);

    EXPECT_FALSE(harness.World.IsZoneResident(zone));
    EXPECT_EQ(harness.World.FindZone(zone), nullptr)
        << "the record outlived residency processing";
}

// Why the window above is not reachable from the streaming policy today: the
// frame flushes lifecycle requests and finalizes residency in adjacent phases,
// both of them before the partition update that decides what to load. This
// pins that pairing. If a phase ever moves such that an update can observe a
// half-detached zone, the load-issue filter starts reissuing a zone that still
// has a live record, and the loader asserts against exactly that.
TEST(AsyncZoneLoaderLifetime, FlushAndResidencyClosePairwiseSoNoUpdateSeesTheWindow)
{
    LoaderHarness harness(0);
    const ZoneId zone{ 0x81 };
    LoadZoneToResident(harness, zone);

    harness.Loader.RequestDestroy(zone);
    harness.Tasks.PumpWork();

    // One frame's worth of the engine's ordering: the drain phase, which runs
    // the destroy commit and flushes lifecycle requests, then the residency
    // phase, with nothing in between.
    harness.Tasks.DrainCompletions();
    harness.World.FlushLifecycleRequests();
    ProcessResidency(harness.World);

    // By the time anything downstream of residency could look, the zone is
    // wholly gone rather than half gone.
    EXPECT_FALSE(harness.World.IsZoneResident(zone));
    EXPECT_EQ(harness.World.FindZone(zone), nullptr);

    // And the zone is loadable again, which is the state the load-issue filter
    // is entitled to assume when residency says no.
    LoadZoneToResident(harness, zone);
    EXPECT_TRUE(harness.World.IsZoneResident(zone));
}

// Partition slots are recycled by value with no generation, so a set captured
// before a detach names whatever zone next takes the slot. Callers are expected
// to rebuild their partition sets from the frame view; this records what the
// stale one does so the expectation is written down somewhere other than a
// reviewer's memory.
TEST(AsyncZoneLoaderLifetime, APartitionSlotIsReusedByValueAfterDetach)
{
    LoaderHarness harness(0);
    const ZoneId first{ 0x90 };
    const ZoneId second{ 0x91 };

    LoadZoneToResident(harness, first);
    const RuntimeZoneRecord* firstRecord = harness.World.FindZone(first);
    ASSERT_NE(firstRecord, nullptr);
    const StoragePartitionId reused = firstRecord->Partition;

    harness.Loader.RequestDestroy(first);
    harness.Tasks.PumpWork();
    harness.Tasks.DrainCompletions();
    harness.World.FlushLifecycleRequests();
    ProcessResidency(harness.World);
    ASSERT_EQ(harness.World.FindZone(first), nullptr);

    LoadZoneToResident(harness, second);
    const RuntimeZoneRecord* secondRecord = harness.World.FindZone(second);
    ASSERT_NE(secondRecord, nullptr);

    EXPECT_EQ(secondRecord->Partition.Value, reused.Value)
        << "the freed slot was not the one handed to the next zone";
    EXPECT_NE(secondRecord->Id, first)
        << "the recycled slot still names the zone that released it";
}
