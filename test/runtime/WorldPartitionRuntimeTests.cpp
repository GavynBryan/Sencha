#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <time/TimeService.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/partition/WorldPartitionManifest.h>
#include <world/partition/WorldPartitionRuntime.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/ZoneRuntime.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

constexpr ZoneId kHub{ 0xa1 };
constexpr ZoneId kHallway{ 0xa2 };
constexpr ZoneId kArena{ 0xa3 };

constexpr const char* kFixtureJson = R"({
  "format_version": 1,
  "name": "StreamingFixture",
  "start_zone": "00000000000000a1",
  "regions": [
    { "id": "00000000000000b1", "name": "Rooms" }
  ],
  "zones": [
    {
      "id": "00000000000000a1",
      "name": "Hub",
      "region": "00000000000000b1",
      "scene": "levels/hub.level.json",
      "bounds": { "min": [-8, 0, -8], "max": [8, 4, 8] },
      "cooked_scene": "levels/hub.cooked.json",
      "cooked_collision": "levels/hub.collision.json",
      "content_hash": "00000000000000d1"
    },
    {
      "id": "00000000000000a2",
      "name": "Hallway",
      "region": "00000000000000b1",
      "scene": "levels/hallway.level.json",
      "bounds": { "min": [9, 0, -2], "max": [20, 4, 2] },
      "cooked_scene": "levels/hallway.cooked.json",
      "cooked_collision": "levels/hallway.collision.json",
      "content_hash": "00000000000000d2"
    },
    {
      "id": "00000000000000a3",
      "name": "Arena",
      "region": "00000000000000b1",
      "scene": "levels/arena.level.json",
      "bounds": { "min": [21, 0, -8], "max": [40, 8, 8] },
      "cooked_scene": "levels/arena.cooked.json",
      "cooked_collision": "levels/arena.collision.json",
      "content_hash": "00000000000000d3"
    }
  ],
  "transitions": [
    { "id": "00000000000000c1", "from": "00000000000000a1", "to": "00000000000000a2" },
    { "id": "00000000000000c2", "from": "00000000000000a2", "to": "00000000000000a1" },
    { "id": "00000000000000c3", "from": "00000000000000a2", "to": "00000000000000a3" },
    { "id": "00000000000000c4", "from": "00000000000000a3", "to": "00000000000000a2" }
  ]
})";

WorldPartitionManifest FixtureManifest()
{
    const auto json = JsonParse(kFixtureJson);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const auto manifest = ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    return *manifest;
}

ZoneLoadRecipe MakeRecipe(std::vector<std::string>* events = nullptr,
                          std::shared_ptr<AssetPreload> preload = {})
{
    ZoneLoadRecipe recipe;
    recipe.Preload = std::move(preload);
    recipe.Build = [events](Registry& registry)
    {
        registry.Components.CreateEntity();
        if (events != nullptr)
            events->push_back("build:" + ZoneIdToString(registry.Zone));
    };
    recipe.Finalize = [events](Registry& registry)
    {
        if (events != nullptr)
            events->push_back("finalize:" + ZoneIdToString(registry.Zone));
    };
    return recipe;
}

void PumpAll(AsyncTaskQueue& tasks)
{
    while (tasks.PumpWork() != 0)
    {
    }
    tasks.DrainCompletions();
}

void Converge(WorldPartitionRuntime& partition,
              AsyncZoneLoader& loader,
              ZoneRuntime& zones,
              AsyncTaskQueue& tasks,
              double deltaSeconds = 0.016)
{
    partition.Update(deltaSeconds, loader, zones);
    PumpAll(tasks);
    partition.Update(0.0, loader, zones);
}

bool HasDemand(const WorldPartitionRuntime& partition, ZoneId zone)
{
    for (const ZoneDemandRecord& record : partition.DemandRecords())
        if (record.Zone == zone)
            return true;
    return false;
}

bool DemandRecordsExplainResidency(const WorldPartitionRuntime& partition,
                                   const ZoneRuntime& zones)
{
    if (!partition.HasFocus())
        return partition.DemandRecords().empty();

    const WorldPartitionManifest* manifest = partition.Manifest();
    if (manifest == nullptr)
        return zones.ZoneCount() == 0;

    bool complete = true;
    zones.VisitZones([&](ZoneId zone, const Registry&, ZoneParticipation)
    {
        if (manifest->FindZone(zone) != nullptr && !HasDemand(partition, zone))
            complete = false;
    });
    return complete;
}

struct TraversalRun
{
    std::vector<std::string> Events;
    bool RecordsExplainedResidency = true;
};

TraversalRun RunScriptedTraversal(uint32_t taskThreads)
{
    TraversalRun run;
    AsyncTaskQueue tasks(taskThreads);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);

    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header)
        {
            ZoneLoadRecipe recipe;
            recipe.Build = [&, id = header.Id](Registry& registry)
            {
                registry.Components.CreateEntity();
                run.Events.push_back("build:" + ZoneIdToString(id));
            };
            recipe.Finalize = [&, id = header.Id](Registry&)
            {
                run.Events.push_back("finalize:" + ZoneIdToString(id));
            };
            return recipe;
        },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.05 });

    partition.SetObserver([&](const WorldPartitionEvent& event)
    {
        if (event.Kind == WorldPartitionEventKind::ParticipationChanged)
        {
            run.Events.push_back(
                "part:" + ZoneIdToString(event.Zone)
                + ":" + (event.Participation.Visible ? "V" : "-")
                + (event.Participation.Physics ? "P" : "-")
                + (event.Participation.Logic ? "L" : "-")
                + (event.Participation.Audio ? "A" : "-"));
        }
        else if (event.Kind == WorldPartitionEventKind::ZoneDestroyed)
        {
            run.Events.push_back("destroy:" + ZoneIdToString(event.Zone));
        }
    });

    std::string error;
    EXPECT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    const auto step = [&](const Vec3d& position, double dt)
    {
        partition.SetFocus(position);
        partition.Update(dt, loader, zones);
        if (taskThreads == 0)
            tasks.PumpWork();
        else
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (tasks.ActiveTaskCount() != 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        tasks.DrainCompletions();
        partition.Update(0.0, loader, zones);
        run.RecordsExplainedResidency =
            run.RecordsExplainedResidency && DemandRecordsExplainResidency(partition, zones);
    };

    step(Vec3d{ 0, 1, 0 }, 0.016);    // Hub
    step(Vec3d{ 10, 1, 0 }, 0.016);   // Hallway
    step(Vec3d{ 22, 1, 0 }, 0.016);   // Arena
    step(Vec3d{ 22, 1, 0 }, 0.060);   // Expire Hub

    return run;
}

} // namespace

TEST(WorldPartitionRuntime, ManifestLoadInitializesStartFocus)
{
    WorldPartitionRuntime partition;
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    EXPECT_TRUE(partition.HasFocus());
    EXPECT_EQ(partition.FocusZone(), kHub);
    EXPECT_EQ(partition.Manifest()->Name, "StreamingFixture");
}

TEST(WorldPartitionRuntime, InvalidFocusIsRejected)
{
    WorldPartitionRuntime partition;
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    EXPECT_FALSE(partition.SetFocus(ZoneId{ 0xdead }));
    EXPECT_EQ(partition.FocusZone(), kHub);
}

TEST(WorldPartitionRuntime, PositionFocusUsesZoneBounds)
{
    WorldPartitionRuntime partition;
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    EXPECT_TRUE(partition.SetFocus(Vec3d{ 12.0, 1.0, 0.0 }));
    EXPECT_EQ(partition.FocusZone(), kHallway);
}

TEST(WorldPartitionRuntime, InitialFocusLoadsFullZoneAndNeighborPreload)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);

    ASSERT_TRUE(zones.IsZoneLoaded(kHub));
    ASSERT_TRUE(zones.IsZoneLoaded(kHallway));
    EXPECT_FALSE(zones.IsZoneLoaded(kArena));

    const ZoneParticipation hub = zones.GetParticipation(kHub);
    EXPECT_TRUE(hub.Visible);
    EXPECT_TRUE(hub.Physics);
    EXPECT_TRUE(hub.Logic);
    EXPECT_TRUE(hub.Audio);

    const ZoneParticipation hallway = zones.GetParticipation(kHallway);
    EXPECT_TRUE(hallway.Visible);
    EXPECT_TRUE(hallway.Physics);
    EXPECT_FALSE(hallway.Logic);
    EXPECT_FALSE(hallway.Audio);
}

TEST(WorldPartitionRuntime, FocusMovePromotesNewZoneAndDemotesOldFocus)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);

    partition.SetFocus(kHallway);
    Converge(partition, loader, zones, tasks);

    const ZoneParticipation hallway = zones.GetParticipation(kHallway);
    EXPECT_TRUE(hallway.Visible);
    EXPECT_TRUE(hallway.Physics);
    EXPECT_TRUE(hallway.Logic);
    EXPECT_TRUE(hallway.Audio);

    const ZoneParticipation hub = zones.GetParticipation(kHub);
    EXPECT_TRUE(hub.Visible);
    EXPECT_TRUE(hub.Physics);
    EXPECT_FALSE(hub.Logic);
    EXPECT_FALSE(hub.Audio);

    EXPECT_TRUE(zones.IsZoneLoaded(kArena));
    const ZoneParticipation arena = zones.GetParticipation(kArena);
    EXPECT_TRUE(arena.Visible);
    EXPECT_TRUE(arena.Physics);
    EXPECT_FALSE(arena.Logic);
    EXPECT_FALSE(arena.Audio);
}

TEST(WorldPartitionRuntime, LingerKeepsThenEvictsUndemandedZone)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);
    partition.SetFocus(kArena);
    Converge(partition, loader, zones, tasks, 0.04);

    ASSERT_TRUE(zones.IsZoneLoaded(kHub));
    const ZoneParticipation lingering = zones.GetParticipation(kHub);
    EXPECT_FALSE(lingering.Visible);
    EXPECT_FALSE(lingering.Physics);
    EXPECT_FALSE(lingering.Logic);
    EXPECT_FALSE(lingering.Audio);

    partition.Update(0.07, loader, zones);
    EXPECT_FALSE(zones.IsZoneLoaded(kHub));
}

TEST(WorldPartitionRuntime, FocusZoneNeverEvictedByLinger)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 0.01 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);
    partition.Update(100.0, loader, zones);

    EXPECT_TRUE(zones.IsZoneLoaded(kHub));
    const ZoneParticipation focus = zones.GetParticipation(kHub);
    EXPECT_TRUE(focus.Visible);
    EXPECT_TRUE(focus.Physics);
    EXPECT_TRUE(focus.Logic);
    EXPECT_TRUE(focus.Audio);
}

TEST(WorldPartitionRuntime, InFlightZoneIsNotIssuedTwice)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    std::vector<ZoneId> issued;
    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header)
        {
            issued.push_back(header.Id);
            return MakeRecipe();
        },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    partition.Update(0.0, loader, zones);
    partition.Update(0.0, loader, zones);

    EXPECT_EQ(issued.size(), 2u);
    EXPECT_EQ(std::count(issued.begin(), issued.end(), kHub), 1);
    EXPECT_EQ(std::count(issued.begin(), issued.end(), kHallway), 1);
}

TEST(WorldPartitionRuntime, LoadOrderIsDeterministicByRankThenPriorityThenId)
{
    WorldPartitionManifest manifest = FixtureManifest();
    ASSERT_EQ(manifest.Transitions.size(), 4u);
    manifest.Transitions[0].PreloadPriority = 0;
    manifest.Transitions[1].PreloadPriority = 0;
    manifest.Transitions[2].PreloadPriority = 9;
    manifest.Transitions[3].PreloadPriority = 9;

    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    std::vector<ZoneId> issued;
    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header)
        {
            issued.push_back(header.Id);
            return MakeRecipe();
        },
        WorldPartitionStreamingConfig{ .HopCount = 2, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(std::move(manifest), &error)) << error;

    partition.SetFocus(kHub);
    partition.Update(0.0, loader, zones);

    const std::vector<ZoneId> expected{ kHub, kHallway, kArena };
    EXPECT_EQ(issued, expected);
}

TEST(WorldPartitionRuntime, PreloadTokenGatesAttachAndParticipation)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    std::shared_ptr<AssetPreload> hallwayPreload = std::make_shared<AssetPreload>();
    std::vector<std::string> events;

    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header)
        {
            return MakeRecipe(&events, header.Id == kHallway ? hallwayPreload : nullptr);
        },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    partition.Update(0.0, loader, zones);
    PumpAll(tasks);
    partition.Update(0.0, loader, zones);

    EXPECT_TRUE(zones.IsZoneLoaded(kHub));
    EXPECT_FALSE(zones.IsZoneLoaded(kHallway));
    EXPECT_TRUE(loader.IsLoading(kHallway));
    EXPECT_TRUE(hallwayPreload->IsPending());

    hallwayPreload->MarkReady();
    tasks.DrainCompletions();
    partition.Update(0.0, loader, zones);

    EXPECT_TRUE(zones.IsZoneLoaded(kHallway));
    const ZoneParticipation hallway = zones.GetParticipation(kHallway);
    EXPECT_TRUE(hallway.Visible);
    EXPECT_TRUE(hallway.Physics);
    EXPECT_FALSE(hallway.Logic);
    EXPECT_FALSE(hallway.Audio);
}

TEST(WorldPartitionRuntime, FailedPreloadCancelsLoadAndAllowsRetry)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    std::shared_ptr<AssetPreload> failed = std::make_shared<AssetPreload>();
    int hallwayAttempts = 0;

    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header)
        {
            if (header.Id == kHallway)
            {
                ++hallwayAttempts;
                return MakeRecipe(nullptr, hallwayAttempts == 1 ? failed : nullptr);
            }
            return MakeRecipe();
        },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    partition.Update(0.0, loader, zones);
    PumpAll(tasks);
    partition.Update(0.0, loader, zones);

    ASSERT_TRUE(loader.IsLoading(kHallway));
    failed->MarkFailed("fixture failure");
    partition.Update(0.0, loader, zones);

    EXPECT_FALSE(loader.IsLoading(kHallway));
    EXPECT_FALSE(zones.IsZoneLoaded(kHallway));

    partition.Update(0.0, loader, zones);
    PumpAll(tasks);
    partition.Update(0.0, loader, zones);

    EXPECT_EQ(hallwayAttempts, 2);
    EXPECT_TRUE(zones.IsZoneLoaded(kHallway));
}

TEST(WorldPartitionRuntime, ObserverReportsParticipationBeforeDestroy)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    std::vector<WorldPartitionEvent> events;

    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.05 });
    partition.SetObserver([&](const WorldPartitionEvent& event) { events.push_back(event); });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);
    partition.SetFocus(kArena);
    Converge(partition, loader, zones, tasks, 0.0);
    partition.Update(0.06, loader, zones);

    std::optional<size_t> dormantIndex;
    std::optional<size_t> destroyIndex;
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].Zone != kHub)
            continue;
        if (events[i].Kind == WorldPartitionEventKind::ParticipationChanged
            && events[i].Participation == ZoneParticipation{})
        {
            dormantIndex = i;
        }
        else if (events[i].Kind == WorldPartitionEventKind::ZoneDestroyed)
        {
            destroyIndex = i;
        }
    }

    ASSERT_TRUE(dormantIndex.has_value());
    ASSERT_TRUE(destroyIndex.has_value());
    EXPECT_LT(*dormantIndex, *destroyIndex);
}

TEST(WorldPartitionRuntime, DemandRecordsMatchLoadedAndInFlightZones)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    partition.Update(0.0, loader, zones);

    ASSERT_EQ(partition.DemandRecords().size(), 2u);
    EXPECT_EQ(partition.DemandRecords()[0].Zone, kHub);
    EXPECT_TRUE(partition.DemandRecords()[0].InFlight);
    EXPECT_EQ(partition.DemandRecords()[1].Zone, kHallway);
    EXPECT_TRUE(partition.DemandRecords()[1].InFlight);

    PumpAll(tasks);
    partition.Update(0.0, loader, zones);

    ASSERT_EQ(partition.DemandRecords().size(), 2u);
    EXPECT_EQ(partition.DemandRecords()[0].Zone, kHub);
    EXPECT_TRUE(partition.DemandRecords()[0].Loaded);
    EXPECT_FALSE(partition.DemandRecords()[0].InFlight);
    EXPECT_EQ(partition.DemandRecords()[1].Zone, kHallway);
    EXPECT_TRUE(partition.DemandRecords()[1].Loaded);
    EXPECT_FALSE(partition.DemandRecords()[1].InFlight);
}

TEST(WorldPartitionRuntime, LingerRecordsExplainLoadedUndemandedZone)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 0.1 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);
    partition.SetFocus(kArena);
    Converge(partition, loader, zones, tasks, 0.04);

    ASSERT_TRUE(zones.IsZoneLoaded(kHub));
    ASSERT_TRUE(HasDemand(partition, kHub));

    const ZoneDemandRecord* hubRecord = nullptr;
    for (const ZoneDemandRecord& record : partition.DemandRecords())
        if (record.Zone == kHub)
            hubRecord = &record;
    ASSERT_NE(hubRecord, nullptr);
    EXPECT_TRUE(hubRecord->Loaded);
    EXPECT_TRUE(hubRecord->Lingering);
    EXPECT_EQ(hubRecord->Kind, ZoneDemandKind::Dormant);
    EXPECT_GT(hubRecord->LingerSecondsRemaining, 0.0);
    EXPECT_TRUE(DemandRecordsExplainResidency(partition, zones));
}

TEST(WorldPartitionRuntime, ClearFocusEmptiesDemandAfterLinger)
{
    AsyncTaskQueue tasks(0);
    ZoneRuntime zones;
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(tasks, zones, runtime);
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return MakeRecipe(); },
        WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 0.01 });
    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error)) << error;

    partition.SetFocus(kHub);
    Converge(partition, loader, zones, tasks);
    partition.ClearFocus();
    partition.Update(0.02, loader, zones);

    EXPECT_FALSE(partition.HasFocus());
    EXPECT_FALSE(zones.IsZoneLoaded(kHub));
    EXPECT_TRUE(partition.DemandRecords().empty());
}

TEST(WorldPartitionTraversal, TraversalEventsHaveCoherentOrdering)
{
    const TraversalRun run = RunScriptedTraversal(0);

    const auto indexOf = [&](const std::string& value) -> ptrdiff_t
    {
        const auto it = std::find(run.Events.begin(), run.Events.end(), value);
        if (it == run.Events.end())
            return -1;
        return std::distance(run.Events.begin(), it);
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
            recipe.Build = [](Registry& registry) { registry.Components.CreateEntity(); };
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
            recipe.Build = [](Registry& registry) { registry.Components.CreateEntity(); };
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
            recipe.Build = [](Registry& registry) { registry.Components.CreateEntity(); };
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
            recipe.Build = [](Registry& registry) { registry.Components.CreateEntity(); };
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
