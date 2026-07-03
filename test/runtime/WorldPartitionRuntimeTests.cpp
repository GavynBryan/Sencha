#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <jobs/AsyncTaskQueue.h>
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
    EXPECT_FALSE(Zones.GetParticipation(kHallway).Any());
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

    EXPECT_TRUE(Zones.GetParticipation(kHallway).Visible);
    EXPECT_FALSE(Zones.GetParticipation(kHub).Any());   // demoted, still resident (neighbor)
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

TEST_F(WorldPartitionRuntimeTest, PositionInNoZoneKeepsFocus)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    Partition.SetFocus(Vec3d{ 1000.0, 0.0, 0.0 });
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
