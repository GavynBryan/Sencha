#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneRuntime.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

// The canonical hand-written cooked fixture: three zones in a doorway chain
// Hub <-> Hallway <-> Arena, one elevated preload priority. The recipes never
// open the cooked paths; they exist so LoadManifest accepts the manifest.
constexpr const char* kFixtureJson = R"({
  "format_version": 1,
  "name": "TraversalFixture",
  "start_zone": "00000000000000a1",
  "regions": [ { "id": "00000000000000b1", "name": "Fixture Region" } ],
  "zones": [
    { "id": "00000000000000a1", "name": "Hub", "region": "00000000000000b1",
      "scene": "levels/hub.level.json",
      "bounds": { "min": [-8, 0, -8], "max": [8, 4, 8] },
      "cooked_scene": "levels/hub.cooked.json",
      "cooked_collision": "levels/hub.collision.json",
      "content_hash": "00000000000000d1" },
    { "id": "00000000000000a2", "name": "Hallway", "region": "00000000000000b1",
      "scene": "levels/hallway.level.json",
      "bounds": { "min": [9, 0, -2], "max": [20, 4, 2] },
      "cooked_scene": "levels/hallway.cooked.json",
      "cooked_collision": "levels/hallway.collision.json",
      "content_hash": "00000000000000d2" },
    { "id": "00000000000000a3", "name": "Arena", "region": "00000000000000b1",
      "scene": "levels/arena.level.json",
      "bounds": { "min": [21, 0, -8], "max": [40, 8, 8] },
      "cooked_scene": "levels/arena.cooked.json",
      "cooked_collision": "levels/arena.collision.json",
      "content_hash": "00000000000000d3" }
  ],
  "transitions": [
    { "id": "00000000000000c1", "from": "00000000000000a1", "to": "00000000000000a2",
      "topology": "doorway", "preload_priority": 1 },
    { "id": "00000000000000c2", "from": "00000000000000a2", "to": "00000000000000a1",
      "topology": "doorway" },
    { "id": "00000000000000c3", "from": "00000000000000a2", "to": "00000000000000a3",
      "topology": "doorway" },
    { "id": "00000000000000c4", "from": "00000000000000a3", "to": "00000000000000a2",
      "topology": "doorway" }
  ]
})";

constexpr ZoneId kHub{ 0xa1 };
constexpr ZoneId kHallway{ 0xa2 };
constexpr ZoneId kArena{ 0xa3 };

WorldPartitionManifest FixtureManifest()
{
    const auto json = JsonParse(kFixtureJson);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const auto manifest = ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    return *manifest;
}

class WorldPartitionRuntimeTest : public ::testing::Test
{
protected:
    WorldPartitionRuntimeTest()
        : Tasks(0)
        , Loader(Tasks, Zones, Runtime)
        , Partition(MakeRecipe(), WorldPartitionStreamingConfig{})
    {
    }

    struct FinalizeObservation
    {
        ZoneId Zone;
        ZoneParticipation Participation;
    };

    [[nodiscard]] ZoneLoadRecipeFn MakeRecipe()
    {
        return [this](const ZoneHeader& header)
        {
            const ZoneId zone = header.Id;
            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            recipe.Finalize = [this, zone](Registry&)
            { Finalized.push_back({ zone, Zones.GetParticipation(zone) }); };
            return recipe;
        };
    }

    void LoadFixture(WorldPartitionStreamingConfig config = {})
    {
        Partition = WorldPartitionRuntime(MakeRecipe(), config);
        std::string error;
        ASSERT_TRUE(Partition.LoadManifest(FixtureManifest(), &error)) << error;
    }

    // One frame: policy update, then the zero-thread queue's work and drain.
    void Step(double dt = 0.0)
    {
        Partition.Update(dt, Loader, Zones);
        Tasks.PumpWork();
        Tasks.DrainCompletions();
    }

    AsyncTaskQueue Tasks;
    ZoneRuntime Zones;
    RuntimeFrameLoop Runtime;
    AsyncZoneLoader Loader;
    WorldPartitionRuntime Partition;
    std::vector<FinalizeObservation> Finalized;
};

} // namespace

TEST_F(WorldPartitionRuntimeTest, LoadManifestRefusesUncookedZones)
{
    WorldPartitionManifest manifest = FixtureManifest();
    manifest.Zones[1].CookedSceneRef.clear();

    std::string error;
    EXPECT_FALSE(Partition.LoadManifest(std::move(manifest), &error));
    EXPECT_NE(error.find("cooked"), std::string::npos);
    EXPECT_FALSE(Partition.HasManifest());
}

TEST_F(WorldPartitionRuntimeTest, LoadManifestRefusesErrorValidation)
{
    WorldPartitionManifest manifest = FixtureManifest();
    TransitionRecord dangling;
    dangling.Id = TransitionId{ 0xc9 };
    dangling.From = kHub;
    dangling.To = ZoneId{ 0xff };   // names no zone
    manifest.Transitions.push_back(dangling);

    std::string error;
    EXPECT_FALSE(Partition.LoadManifest(std::move(manifest), &error));
    EXPECT_NE(error.find("partition.transition.endpoint_missing"), std::string::npos);
}

TEST_F(WorldPartitionRuntimeTest, NeighborLoadsDormantOnUpdate)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    Step();

    ASSERT_TRUE(Zones.IsZoneLoaded(kHallway));
    bool sawHallway = false;
    for (const FinalizeObservation& observation : Finalized)
        if (observation.Zone == kHallway)
        {
            sawHallway = true;
            EXPECT_FALSE(observation.Participation.Any());   // dormant at attach
        }
    EXPECT_TRUE(sawHallway);
}

TEST_F(WorldPartitionRuntimeTest, FocusZoneParticipationIsFull)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    Step();   // loads attach dormant
    Step();   // participation converges

    const ZoneParticipation participation = Zones.GetParticipation(kHub);
    EXPECT_TRUE(participation.Visible);
    EXPECT_TRUE(participation.Physics);
    EXPECT_TRUE(participation.Logic);
    EXPECT_TRUE(participation.Audio);
    // The neighbor render-preloads: visible, collidable, inert.
    const ZoneParticipation neighbor = Zones.GetParticipation(kHallway);
    EXPECT_TRUE(neighbor.Visible);
    EXPECT_TRUE(neighbor.Physics);
    EXPECT_FALSE(neighbor.Logic);
    EXPECT_FALSE(neighbor.Audio);
}

TEST_F(WorldPartitionRuntimeTest, FocusChangeDemotesOldFocusToDormant)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    Step();
    Step();
    ASSERT_TRUE(Zones.GetParticipation(kHub).Visible);

    Partition.SetFocus(kHallway);
    Step();

    EXPECT_TRUE(Zones.GetParticipation(kHallway).Logic);   // promoted to full
    // The old focus demotes to the neighbor render preload, still resident.
    EXPECT_TRUE(Zones.GetParticipation(kHub).Visible);
    EXPECT_FALSE(Zones.GetParticipation(kHub).Logic);
    EXPECT_TRUE(Zones.IsZoneLoaded(kHub));
}

TEST_F(WorldPartitionRuntimeTest, LingerThenDestroy)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 3.0 });
    Partition.SetFocus(kHub);
    Partition.PinZone(kHallway, ZoneParticipation{});
    Step();
    ASSERT_TRUE(Zones.IsZoneLoaded(kHallway));

    Partition.UnpinZone(kHallway);
    Step(1.0);   // linger 1.0: resident
    EXPECT_TRUE(Zones.IsZoneLoaded(kHallway));
    Step(1.9);   // linger 2.9: resident
    EXPECT_TRUE(Zones.IsZoneLoaded(kHallway));
    Step(0.1);   // linger 3.0: at the budget, destroyed
    EXPECT_FALSE(Zones.IsZoneLoaded(kHallway));
    EXPECT_TRUE(Zones.IsZoneLoaded(kHub));
}

TEST_F(WorldPartitionRuntimeTest, LingerClockResetsOnRedemand)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 3.0 });
    Partition.SetFocus(kHub);
    Partition.PinZone(kHallway, ZoneParticipation{});
    Step();

    Partition.UnpinZone(kHallway);
    Step(2.0);   // linger 2.0
    ASSERT_TRUE(Zones.IsZoneLoaded(kHallway));

    Partition.PinZone(kHallway, ZoneParticipation{});
    Step();      // demanded again: clock drops
    Partition.UnpinZone(kHallway);
    Step(2.0);   // linger restarts at 2.0, below the budget

    EXPECT_TRUE(Zones.IsZoneLoaded(kHallway));
    Step(1.0);   // 3.0: destroyed
    EXPECT_FALSE(Zones.IsZoneLoaded(kHallway));
}

TEST_F(WorldPartitionRuntimeTest, PinKeepsZoneResidentPastLinger)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 3.0 });
    Partition.SetFocus(kHub);
    Partition.PinZone(kArena, ZoneParticipation{ .Logic = true });
    Step();
    Step();

    for (int i = 0; i < 10; ++i)
        Step(10.0);

    EXPECT_TRUE(Zones.IsZoneLoaded(kArena));
    EXPECT_TRUE(Zones.GetParticipation(kArena).Logic);
}

TEST_F(WorldPartitionRuntimeTest, UndemandedInFlightLoadIsCancelled)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0 });
    Partition.SetFocus(kHub);
    Partition.PinZone(kHallway, ZoneParticipation{});
    Partition.Update(0.0, Loader, Zones);   // issues both loads, nothing pumped
    ASSERT_TRUE(Loader.IsLoading(kHallway));

    Partition.UnpinZone(kHallway);
    Partition.Update(0.0, Loader, Zones);   // zero-thread queue: cancel succeeds

    EXPECT_FALSE(Loader.IsLoading(kHallway));
    Tasks.PumpWork();
    Tasks.DrainCompletions();
    EXPECT_FALSE(Zones.IsZoneLoaded(kHallway));
    EXPECT_TRUE(Zones.IsZoneLoaded(kHub));
}

TEST_F(WorldPartitionRuntimeTest, UncancellableInFlightLoadRetriesAndReportsLingering)
{
    // A one-thread queue plus a build gated on an atomic: CancelLoad fails
    // while the build runs, the runtime reports the zone Lingering and retries.
    AsyncTaskQueue tasks(1);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    std::atomic<bool> started{ false };
    std::atomic<bool> release{ false };
    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header) -> ZoneLoadRecipe
        {
            ZoneLoadRecipe recipe;
            if (header.Id == kHallway)
                recipe.Build = [&](Registry&)
                {
                    started = true;
                    while (!release)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                };
            else
                recipe.Build = [](Registry&) {};
            return recipe;
        },
        WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 0.0 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    partition.PinZone(kHallway, ZoneParticipation{});
    partition.Update(0.0, loader, zones);
    while (!started)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    partition.UnpinZone(kHallway);
    partition.Update(0.0, loader, zones);   // CancelLoad fails mid-build

    EXPECT_TRUE(loader.IsLoading(kHallway));
    bool reported = false;
    for (const ZoneDemandRecord& record : partition.DemandRecords())
        if (record.Zone == kHallway)
            reported = record.Sources.Lingering;
    EXPECT_TRUE(reported);

    release = true;
    // The build finishes and attaches; the runtime then destroys the
    // unwanted zone through the (zero-length) linger path.
    for (int i = 0; i < 100 && loader.IsLoading(kHallway); ++i)
    {
        tasks.DrainCompletions();
        partition.Update(0.1, loader, zones);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    partition.Update(0.1, loader, zones);
    tasks.DrainCompletions();   // destruction lands at the drain, as in a frame
    EXPECT_FALSE(zones.IsZoneLoaded(kHallway));
}

TEST_F(WorldPartitionRuntimeTest, FocusResolutionPrefersCurrentZoneOnOverlap)
{
    // Hallway's bounds stretched to overlap Hub over x in [7, 8].
    WorldPartitionManifest manifest = FixtureManifest();
    manifest.Zones[1].Bounds = Aabb3d::FromMinMax(Vec3d{ 7, 0, -2 }, Vec3d{ 20, 4, 2 });
    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(std::move(manifest), &error)) << error;

    Partition.SetFocus(kHub);
    Partition.SetFocus(Vec3d{ 7.5, 1.0, 0.0 });   // inside both
    EXPECT_EQ(Partition.FocusZone(), kHub);

    Partition.SetFocus(kHallway);
    Partition.SetFocus(Vec3d{ 7.5, 1.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kHallway);
}

TEST_F(WorldPartitionRuntimeTest, PositionInNoZoneFocusesNearestZone)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    // Far east of every bounds: the nearest zone (Arena, x up to 40) wins;
    // the stale previous focus does not survive.
    Partition.SetFocus(Vec3d{ 1000.0, 0.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kArena);
    // Hovering above the Hub's box focuses the Hub: the floor-slab case
    // (derived bounds hug geometry; the pawn transform rides above them).
    Partition.SetFocus(Vec3d{ 0.0, 10.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kHub);
}

TEST_F(WorldPartitionRuntimeTest, SmallestVolumeWinsTiesById)
{
    // Hallway stretched over the sample point; Hub is far larger, so the
    // smaller Hallway wins when the current focus (Arena) is no candidate.
    WorldPartitionManifest manifest = FixtureManifest();
    manifest.Zones[1].Bounds = Aabb3d::FromMinMax(Vec3d{ 7, 0, -2 }, Vec3d{ 20, 4, 2 });
    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(std::move(manifest), &error)) << error;
    Partition.SetFocus(kArena);
    Partition.SetFocus(Vec3d{ 7.5, 1.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kHallway);

    // Identical bounds tie: the lower zone id wins.
    WorldPartitionManifest tie = FixtureManifest();
    tie.Zones[1].Bounds = tie.Zones[0].Bounds;
    ASSERT_TRUE(Partition.LoadManifest(std::move(tie), &error)) << error;
    Partition.SetFocus(kArena);
    Partition.SetFocus(Vec3d{ 0.0, 1.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kHub);
}

TEST_F(WorldPartitionRuntimeTest, FocusZoneIsNeverUnloaded)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 0.0,
                                               .ResidentZoneCap = 1 });
    Partition.SetFocus(kHub);
    Step();
    Step();

    for (int i = 0; i < 20; ++i)
        Step(100.0);

    EXPECT_TRUE(Zones.IsZoneLoaded(kHub));
    EXPECT_TRUE(Zones.GetParticipation(kHub).Visible);
}

TEST_F(WorldPartitionRuntimeTest, DemandRecordsAreDeterministicallyOrdered)
{
    LoadFixture();
    Partition.SetFocus(kHallway);
    Step();
    Step();

    const auto snapshot = [&]
    {
        std::vector<ZoneDemandRecord> records;
        for (const ZoneDemandRecord& record : Partition.DemandRecords())
            records.push_back(record);
        return records;
    };
    Partition.Update(0.0, Loader, Zones);
    const auto first = snapshot();
    Partition.Update(0.0, Loader, Zones);
    const auto second = snapshot();

    ASSERT_EQ(first.size(), second.size());
    ASSERT_GE(first.size(), 3u);   // Hallway plus both neighbors
    for (size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(first[i].Zone, second[i].Zone);
        if (i > 0)
            EXPECT_LT(first[i - 1].Zone.Value, first[i].Zone.Value);
        EXPECT_EQ(first[i].Sources.Focus, second[i].Sources.Focus);
        EXPECT_EQ(first[i].Sources.Neighbor, second[i].Sources.Neighbor);
        EXPECT_EQ(first[i].Sources.Lingering, second[i].Sources.Lingering);
    }
}

//=============================================================================
// R4: the traversal gate, headless. When the traversal-hitch harness lands,
// this scripted walk becomes one of its scripts; coordinate, do not duplicate.
//=============================================================================

namespace
{

// Scripted Hub-to-Arena walk. Every step sets focus from the position, runs
// one Update, then settles all in-flight loads, so the residency event
// sequence depends only on policy, never on task-thread timing.
struct TraversalRun
{
    std::vector<std::string> Events;
    std::vector<std::pair<ZoneId, double>> AttachPositions;   // zone, x at attach
    std::vector<ZoneParticipation> AttachParticipations;
    bool RecordsExplainedResidency = true;
};

TraversalRun RunScriptedTraversal(unsigned taskThreads)
{
    AsyncTaskQueue tasks(taskThreads);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    TraversalRun run;
    double currentX = 0.0;

    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header) -> ZoneLoadRecipe
        {
            run.Events.push_back("issue:" + ZoneIdToString(header.Id));
            const ZoneId zone = header.Id;
            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            recipe.Finalize = [&run, &zones, &currentX, zone](Registry&)
            {
                run.Events.push_back("attach:" + ZoneIdToString(zone));
                run.AttachPositions.push_back({ zone, currentX });
                run.AttachParticipations.push_back(zones.GetParticipation(zone));
            };
            return recipe;
        },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.5 });
    std::string error;
    if (!partition.LoadManifest(FixtureManifest(), &error))
    {
        ADD_FAILURE() << error;
        return run;
    }
    partition.SetFocus(kHub);

    const ZoneId zoneOrder[] = { kHub, kHallway, kArena };
    bool wasLoaded[3] = { false, false, false };
    ZoneParticipation lastParticipation[3];

    const auto participationTag = [](const ZoneParticipation& p)
    {
        std::string tag = "----";
        if (p.Visible) tag[0] = 'V';
        if (p.Physics) tag[1] = 'P';
        if (p.Logic) tag[2] = 'L';
        if (p.Audio) tag[3] = 'A';
        return tag;
    };

    for (int step = 0; step <= 60; ++step)
    {
        currentX = 0.5 * step;   // Hub center to Arena center
        partition.SetFocus(Vec3d{ currentX, 1.0, 0.0 });
        partition.Update(0.25, loader, zones);

        // Settle: all in-flight loads attach before the step's observations.
        const auto anyLoading = [&]
        {
            for (ZoneId zone : zoneOrder)
                if (loader.IsLoading(zone))
                    return true;
            return false;
        };
        if (taskThreads == 0)
        {
            do
            {
                tasks.PumpWork();
                tasks.DrainCompletions();
            } while (anyLoading());
        }
        else
        {
            do
            {
                tasks.DrainCompletions();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (anyLoading());
        }

        for (size_t i = 0; i < 3; ++i)
        {
            const ZoneId zone = zoneOrder[i];
            const bool loaded = zones.IsZoneLoaded(zone);
            if (loaded != wasLoaded[i])
                run.Events.push_back((loaded ? "load:" : "destroy:") + ZoneIdToString(zone));
            const ZoneParticipation participation =
                loaded ? zones.GetParticipation(zone) : ZoneParticipation{};
            if (participationTag(participation) != participationTag(lastParticipation[i]))
                run.Events.push_back("part:" + ZoneIdToString(zone) + ":"
                                     + participationTag(participation));
            wasLoaded[i] = loaded;
            lastParticipation[i] = participation;

            if (loaded)
            {
                bool explained = false;
                for (const ZoneDemandRecord& record : partition.DemandRecords())
                    if (record.Zone == zone)
                        explained = record.Sources.Focus || record.Sources.Pinned
                            || record.Sources.Neighbor || record.Sources.Lingering;
                run.RecordsExplainedResidency &= explained;
            }
        }
    }
    return run;
}

} // namespace

TEST(WorldPartitionTraversal, TraversalAttachesDormantAheadOfCrossing)
{
    const TraversalRun run = RunScriptedTraversal(0);

    for (const ZoneParticipation& participation : run.AttachParticipations)
        EXPECT_FALSE(participation.Any());   // never visible-on-attach

    // Ahead of the crossing: each zone attached before the position reached it.
    for (const auto& [zone, x] : run.AttachPositions)
    {
        if (zone == kHallway)
            EXPECT_LT(x, 9.0);
        if (zone == kArena)
            EXPECT_LT(x, 21.0);
    }
    bool arenaAttached = false;
    for (const auto& [zone, x] : run.AttachPositions)
        arenaAttached |= zone == kArena;
    EXPECT_TRUE(arenaAttached);
}

TEST(WorldPartitionTraversal, TraversalFlipsParticipationOnEntryAndUnloadsBehind)
{
    const TraversalRun run = RunScriptedTraversal(0);

    const auto indexOf = [&](const std::string& event)
    {
        for (size_t i = 0; i < run.Events.size(); ++i)
            if (run.Events[i] == event)
                return static_cast<ptrdiff_t>(i);
        return static_cast<ptrdiff_t>(-1);
    };

    const ptrdiff_t hallwayFull = indexOf("part:" + ZoneIdToString(kHallway) + ":VPLA");
    const ptrdiff_t hubDormant = indexOf("part:" + ZoneIdToString(kHub) + ":----");
    const ptrdiff_t hubDestroyed = indexOf("destroy:" + ZoneIdToString(kHub));

    ASSERT_GE(hallwayFull, 0);   // flips to full on entry
    ASSERT_GE(hubDormant, 0);    // the old focus demotes
    ASSERT_GE(hubDestroyed, 0);  // and unloads after the linger budget
    EXPECT_LT(hubDormant, hubDestroyed);
}

TEST(WorldPartitionTraversal, TraversalDemandRecordsExplainResidencyEveryStep)
{
    const TraversalRun run = RunScriptedTraversal(0);
    EXPECT_TRUE(run.RecordsExplainedResidency);
}

TEST(WorldPartitionTraversal, TraversalIdenticalAcrossTaskThreadCounts)
{
    const TraversalRun serial = RunScriptedTraversal(0);
    const TraversalRun threaded = RunScriptedTraversal(1);

    EXPECT_EQ(serial.Events, threaded.Events);
}

TEST(WorldPartitionTraversal, TraversalRunsFullTickBudget)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    WorldPartitionRuntime partition(
        [](const ZoneHeader&) -> ZoneLoadRecipe
        {
            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            return recipe;
        },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;
    partition.SetFocus(kHub);

    FrameDriver driver(runtime);
    double x = 0.0;
    driver.Register(FramePhase::DrainAsyncTasks,
                    [&](PhaseContext&)
                    {
                        tasks.PumpWork();
                        tasks.DrainCompletions();
                    });
    driver.Register(FramePhase::ScheduleTicks,
                    [&](PhaseContext& ctx) { ctx.Runtime->ScheduleFixedTicks(); });
    driver.Register(FramePhase::Update,
                    [&](PhaseContext&)
                    {
                        x = std::min(30.0, x + 0.5);
                        partition.SetFocus(Vec3d{ x, 1.0, 0.0 });
                        partition.Update(1.0 / 60.0, loader, zones);
                    });

    // Streaming work must never eat a scheduled tick: every frame runs exactly
    // the budget ScheduleFixedTicks granted.
    for (int frame = 0; frame < 120; ++frame)
    {
        driver.StepOnce();
        const RuntimeFrameSnapshot& snapshot = runtime.GetCurrentFrame();
        EXPECT_EQ(snapshot.FixedTicks, snapshot.Budget.TicksToRunThisFrame);
    }
    EXPECT_TRUE(zones.IsZoneLoaded(kArena));
}

TEST(WorldPartitionTraversal, TraversalNeighborIsVisibleBeforeCrossing)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) -> ZoneLoadRecipe
        {
            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            return recipe;
        },
        WorldPartitionStreamingConfig{});
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    // Standing in Hub: the hallway attaches dormant, then converges to the
    // render preload, all before the player reaches the doorway.
    partition.SetFocus(Vec3d{ 0, 1, 0 });
    for (int i = 0; i < 3; ++i)
    {
        partition.Update(0.016, loader, zones);
        tasks.PumpWork();
        tasks.DrainCompletions();
    }

    ASSERT_TRUE(zones.IsZoneLoaded(kHallway));
    const ZoneParticipation hallway = zones.GetParticipation(kHallway);
    EXPECT_TRUE(hallway.Visible);    // readable through the doorway
    EXPECT_TRUE(hallway.Physics);    // the threshold lands on resident colliders
    EXPECT_FALSE(hallway.Logic);     // nothing simulates until entry
}

namespace
{

// Two topologies in one world: a graph region of rooms (streams by authored
// edges) and a proximity region of field cells with no edge between the
// cells, entered through one authored edge from the arena.
constexpr const char* kTwoRegionFixtureJson = R"({
  "format_version": 1,
  "name": "TwoRegionFixture",
  "start_zone": "00000000000000a1",
  "regions": [
    { "id": "00000000000000b1", "name": "Rooms" },
    { "id": "00000000000000b2", "name": "Fields", "streaming": { "radius": 20 } }
  ],
  "zones": [
    { "id": "00000000000000a1", "name": "Hub", "region": "00000000000000b1",
      "scene": "levels/hub.level.json",
      "bounds": { "min": [-8, 0, -8], "max": [8, 4, 8] },
      "cooked_scene": "levels/hub.cooked.json",
      "cooked_collision": "levels/hub.collision.json",
      "content_hash": "00000000000000d1" },
    { "id": "00000000000000a2", "name": "Hallway", "region": "00000000000000b1",
      "scene": "levels/hallway.level.json",
      "bounds": { "min": [9, 0, -2], "max": [20, 4, 2] },
      "cooked_scene": "levels/hallway.cooked.json",
      "cooked_collision": "levels/hallway.collision.json",
      "content_hash": "00000000000000d2" },
    { "id": "00000000000000a3", "name": "Arena", "region": "00000000000000b1",
      "scene": "levels/arena.level.json",
      "bounds": { "min": [21, 0, -8], "max": [40, 8, 8] },
      "cooked_scene": "levels/arena.cooked.json",
      "cooked_collision": "levels/arena.collision.json",
      "content_hash": "00000000000000d3" },
    { "id": "00000000000000a7", "name": "Vault", "region": "00000000000000b1",
      "scene": "levels/vault.level.json",
      "bounds": { "min": [9, 0, 3], "max": [20, 4, 14] },
      "cooked_scene": "levels/vault.cooked.json",
      "cooked_collision": "levels/vault.collision.json",
      "content_hash": "00000000000000d7" },
    { "id": "00000000000000a4", "name": "FieldWest", "region": "00000000000000b2",
      "scene": "levels/field_west.level.json",
      "bounds": { "min": [100, 0, -8], "max": [116, 4, 8] },
      "cooked_scene": "levels/field_west.cooked.json",
      "cooked_collision": "levels/field_west.collision.json",
      "content_hash": "00000000000000d4" },
    { "id": "00000000000000a5", "name": "FieldMid", "region": "00000000000000b2",
      "scene": "levels/field_mid.level.json",
      "bounds": { "min": [116, 0, -8], "max": [132, 4, 8] },
      "cooked_scene": "levels/field_mid.cooked.json",
      "cooked_collision": "levels/field_mid.collision.json",
      "content_hash": "00000000000000d5" },
    { "id": "00000000000000a6", "name": "FieldEast", "region": "00000000000000b2",
      "scene": "levels/field_east.level.json",
      "bounds": { "min": [132, 0, -8], "max": [148, 4, 8] },
      "cooked_scene": "levels/field_east.cooked.json",
      "cooked_collision": "levels/field_east.collision.json",
      "content_hash": "00000000000000d6" }
  ],
  "transitions": [
    { "id": "00000000000000c1", "from": "00000000000000a1", "to": "00000000000000a2" },
    { "id": "00000000000000c2", "from": "00000000000000a2", "to": "00000000000000a1" },
    { "id": "00000000000000c3", "from": "00000000000000a2", "to": "00000000000000a3" },
    { "id": "00000000000000c4", "from": "00000000000000a3", "to": "00000000000000a2" },
    { "id": "00000000000000c7", "from": "00000000000000a2", "to": "00000000000000a7",
      "preload_priority": 5 },
    { "id": "00000000000000c8", "from": "00000000000000a7", "to": "00000000000000a2" },
    { "id": "00000000000000c5", "from": "00000000000000a3", "to": "00000000000000a4" },
    { "id": "00000000000000c6", "from": "00000000000000a4", "to": "00000000000000a3" }
  ]
})";

WorldPartitionManifest TwoRegionManifest()
{
    const auto json = JsonParse(kTwoRegionFixtureJson);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const auto manifest = ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    return *manifest;
}

// One runtime, one focus, one update: the demand set that focus produces.
std::vector<ZoneId> DemandSetAt(WorldPartitionManifest manifest, ZoneId focus)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) -> ZoneLoadRecipe
        {
            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            return recipe;
        },
        WorldPartitionStreamingConfig{});
    std::string error;
    if (!partition.LoadManifest(std::move(manifest), &error))
    {
        ADD_FAILURE() << error;
        return {};
    }
    partition.SetFocus(focus);
    partition.Update(0.0, loader, zones);
    std::vector<ZoneId> demanded;
    for (const ZoneDemandRecord& record : partition.DemandRecords())
        demanded.push_back(record.Zone);
    return demanded;
}

} // namespace

TEST(WorldPartitionRegionStreaming, FocusRegionSelectsDemandShape)
{
    const auto contains = [](const std::vector<ZoneId>& set, uint64_t id)
    {
        for (ZoneId zone : set)
            if (zone == ZoneId{ id })
                return true;
        return false;
    };

    // Graph-region focus: the authored graph is the shape (hop 1, no radius);
    // the field cells stay out however near or far they sit.
    const auto graphSet = DemandSetAt(TwoRegionManifest(), ZoneId{ 0xa1 });
    EXPECT_TRUE(contains(graphSet, 0xa1));
    EXPECT_TRUE(contains(graphSet, 0xa2));
    EXPECT_FALSE(contains(graphSet, 0xa3));
    EXPECT_FALSE(contains(graphSet, 0xa4));
    EXPECT_FALSE(contains(graphSet, 0xa5));
    EXPECT_FALSE(contains(graphSet, 0xa6));

    // Grid-region focus: proximity is the shape; no manifest edge connects
    // the cells, and the ring still loads.
    const auto gridSet = DemandSetAt(TwoRegionManifest(), ZoneId{ 0xa5 });
    EXPECT_TRUE(contains(gridSet, 0xa5));
    EXPECT_TRUE(contains(gridSet, 0xa4));
    EXPECT_TRUE(contains(gridSet, 0xa6));
    EXPECT_FALSE(contains(gridSet, 0xa1));
    EXPECT_FALSE(contains(gridSet, 0xa2));
    EXPECT_FALSE(contains(gridSet, 0xa3));
}

TEST(WorldPartitionRegionStreaming, RegionHopOverrideOrdersDeepLoadsByRank)
{
    WorldPartitionManifest manifest = TwoRegionManifest();
    ASSERT_EQ(manifest.Regions[0].Name, "Rooms");
    manifest.Regions[0].Streaming.HopCount = 2;

    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    std::vector<ZoneId> issued;
    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header) -> ZoneLoadRecipe
        {
            issued.push_back(header.Id);
            ZoneLoadRecipe recipe;
            recipe.Build = [](Registry& registry) { registry.Entities.Create(); };
            return recipe;
        },
        WorldPartitionStreamingConfig{});   // base hop count stays 1
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(std::move(manifest), &error)) << error;

    partition.SetFocus(ZoneId{ 0xa1 });
    partition.Update(0.0, loader, zones);

    // The override reaches hop 2 in the ranks BFS too, so the deeper zones
    // issue in rank order: the priority-5 vault ahead of the priority-0
    // arena. If only the demand call resolved the override, both would be
    // rank-less and issue by ascending id (arena first).
    const std::vector<ZoneId> expected{ ZoneId{ 0xa1 }, ZoneId{ 0xa2 }, ZoneId{ 0xa7 },
                                        ZoneId{ 0xa3 } };
    EXPECT_EQ(issued, expected);
}
