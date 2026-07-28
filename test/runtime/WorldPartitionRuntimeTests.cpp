#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneLoadPackage.h>

#include "ZoneDockFixture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

// A legacy-read fixture converted to cooked dock endpoints after parsing:
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
      "topology": "doorway" },
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
    const std::optional<JsonValue> json = JsonParse(kFixtureJson);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const std::optional<WorldPartitionManifest> manifest =
        ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    WorldPartitionManifest cooked = *manifest;
    cooked.Transitions.clear();
    AddFixtureDockPair(cooked, 0xd1, kHub, kHallway, Vec3d{ 8.5, 2, 0 });
    AddFixtureDockPair(cooked, 0xd2, kHallway, kArena, Vec3d{ 20.5, 2, 0 });
    return cooked;
}

WorldComponentSchema EmptySchema()
{
    WorldComponentSchema schema;
    schema.Seal();
    return schema;
}

ZoneParticipation FullParticipation()
{
    return ZoneParticipation{
        .Visible = true,
        .Physics = true,
        .Logic = true,
        .Audio = true,
    };
}

struct FinalizeObservation
{
    ZoneId Zone;
    RuntimeZoneLoadState State = RuntimeZoneLoadState::Loading;
    ZoneParticipation Participation;
};

class WorldPartitionRuntimeTest : public ::testing::Test
{
protected:
    WorldPartitionRuntimeTest()
        : Tasks(0)
        , Schema(EmptySchema())
        , RuntimeWorldState(Schema)
        , SceneContext(Logging)
        , Loader(
            Tasks,
            RuntimeWorldState,
            Schema,
            Serializers,
            SceneContext,
            Runtime)
        , Partition(MakeRecipe(), WorldPartitionStreamingConfig{})
    {
    }

    ZoneLoadRecipeFn MakeRecipe()
    {
        return [this](const ZoneHeader& header)
        {
            const ZoneId zone = header.Id;
            ZoneLoadRecipe recipe;
            recipe.Build = [](ZoneLoadPackage& package)
            {
                package.CreateEntity();
            };
            recipe.Finalize = [this, zone](
                RuntimeWorld&,
                RuntimeZoneRecord& record)
            {
                Finalized.push_back(FinalizeObservation{
                    .Zone = zone,
                    .State = record.State,
                    .Participation = record.Participation,
                });
                return true;
            };
            return recipe;
        };
    }

    void LoadFixture(WorldPartitionStreamingConfig config = {})
    {
        Partition = WorldPartitionRuntime(MakeRecipe(), config);
        std::string error;
        ASSERT_TRUE(Partition.LoadManifest(FixtureManifest(), &error))
            << error;
    }

    void ProcessResidency()
    {
        (void)RuntimeWorldState.BeginResidencyProcessing();
        RuntimeWorldState.FinalizeResidencyProcessing();
    }

    void Step(double dt = 0.0)
    {
        Partition.Update(dt, Loader, RuntimeWorldState);
        RuntimeWorldState.FlushLifecycleRequests();
        Tasks.PumpWork();
        Tasks.DrainCompletions();
        RuntimeWorldState.FlushLifecycleRequests();
        ProcessResidency();
    }

    const RuntimeZoneRecord* Zone(ZoneId zone) const
    {
        return RuntimeWorldState.FindZone(zone);
    }

    LoggingProvider Logging;
    AsyncTaskQueue Tasks;
    WorldComponentSchema Schema;
    RuntimeWorld RuntimeWorldState;
    ComponentSerializerRegistry Serializers;
    SceneSerializationContext SceneContext;
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

TEST_F(WorldPartitionRuntimeTest, LoadManifestRefusesUnmigratedLegacyTransitions)
{
    WorldPartitionManifest manifest = FixtureManifest();
    TransitionRecord dangling;
    dangling.Id = TransitionId{ 0xc9 };
    dangling.From = kHub;
    dangling.To = ZoneId{ 0xff };
    manifest.Transitions.push_back(dangling);

    std::string error;
    EXPECT_FALSE(Partition.LoadManifest(std::move(manifest), &error));
    EXPECT_NE(error.find("legacy transitions"), std::string::npos);
}

TEST_F(WorldPartitionRuntimeTest, FocusAndNeighborAttachHiddenThenConvergeParticipation)
{
    LoadFixture();
    Partition.SetFocus(kHub);

    Step();

    ASSERT_NE(Zone(kHub), nullptr);
    ASSERT_NE(Zone(kHallway), nullptr);
    EXPECT_FALSE(Zone(kHub)->Participation.Any());
    EXPECT_FALSE(Zone(kHallway)->Participation.Any());

    bool sawHallwayFinalize = false;
    for (const FinalizeObservation& observation : Finalized)
    {
        if (observation.Zone != kHallway)
            continue;
        sawHallwayFinalize = true;
        EXPECT_EQ(observation.State, RuntimeZoneLoadState::Importing);
        EXPECT_FALSE(observation.Participation.Any());
    }
    EXPECT_TRUE(sawHallwayFinalize);

    Step();

    ASSERT_NE(Zone(kHub), nullptr);
    EXPECT_EQ(Zone(kHub)->Participation, FullParticipation());

    ASSERT_NE(Zone(kHallway), nullptr);
    EXPECT_TRUE(Zone(kHallway)->Participation.Visible);
    EXPECT_TRUE(Zone(kHallway)->Participation.Physics);
    EXPECT_FALSE(Zone(kHallway)->Participation.Logic);
    EXPECT_FALSE(Zone(kHallway)->Participation.Audio);
}

TEST_F(WorldPartitionRuntimeTest, FocusChangePromotesNewFocusAndDemotesOldFocus)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    Step();
    Step();

    Partition.SetFocus(kHallway);
    Step();

    ASSERT_NE(Zone(kHallway), nullptr);
    EXPECT_EQ(Zone(kHallway)->Participation, FullParticipation());
    ASSERT_NE(Zone(kHub), nullptr);
    EXPECT_TRUE(Zone(kHub)->Participation.Visible);
    EXPECT_TRUE(Zone(kHub)->Participation.Physics);
    EXPECT_FALSE(Zone(kHub)->Participation.Logic);
    EXPECT_FALSE(Zone(kHub)->Participation.Audio);
}

TEST_F(WorldPartitionRuntimeTest, DockCrossingWaitsForDestinationPhysicsResidency)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0 });
    Partition.SetFocus(Vec3d{ 0, 1, 0 });
    Step();
    Step();

    Partition.SetFocus(Vec3d{ 10, 1, 0 });
    Step();
    EXPECT_EQ(Partition.FocusZone(), kHub);
    EXPECT_EQ(Partition.LastTraversal().Status,
              DockTraversalStatus::BlockedDestinationNotReady);
    EXPECT_LT(Partition.LastTraversal().SafeSourcePosition.X, 8.5f);
    EXPECT_EQ(Partition.LateTraversalCount(), 1u);

    Partition.PinZone(kHallway, ZoneParticipation{ .Physics = true });
    Step();
    Step();
    Partition.SetFocus(Vec3d{ 10, 1, 0 });
    Step();
    EXPECT_EQ(Partition.LastTraversal().Status, DockTraversalStatus::Crossed);
    EXPECT_EQ(Partition.FocusZone(), kHallway);
}

TEST_F(WorldPartitionRuntimeTest, CrossingRecordsTraversalGraceReason)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 1, .LingerSeconds = 2.0 });
    Partition.SetFocus(Vec3d{ 0, 1, 0 });
    Step();
    Step();

    Partition.SetFocus(Vec3d{ 10, 1, 0 });
    Step(0.1);
    ASSERT_EQ(Partition.LastTraversal().Status, DockTraversalStatus::Crossed);

    const ZoneDemandRecord* source = nullptr;
    for (const ZoneDemandRecord& record : Partition.DemandRecords())
        if (record.Zone == kHub)
            source = &record;
    ASSERT_NE(source, nullptr);
    EXPECT_TRUE(source->Sources.TraversalGrace);
    EXPECT_TRUE(std::any_of(source->Reasons.begin(), source->Reasons.end(),
                            [](const ZoneDemandReasonRecord& reason)
                            { return reason.Reason == ZoneDemandReason::TraversalGrace; }));
}

TEST_F(WorldPartitionRuntimeTest, LingerExpiresThroughQueuedFinalDetach)
{
    LoadFixture(WorldPartitionStreamingConfig{
        .HopCount = 0,
        .LingerSeconds = 3.0,
    });
    Partition.SetFocus(kHub);
    Partition.PinZone(kHallway, ZoneParticipation{});
    Step();
    ASSERT_NE(Zone(kHallway), nullptr);

    Partition.UnpinZone(kHallway);
    Step(1.0);
    EXPECT_NE(Zone(kHallway), nullptr);
    Step(1.9);
    EXPECT_NE(Zone(kHallway), nullptr);
    Step(0.1);

    EXPECT_EQ(Zone(kHallway), nullptr);
    EXPECT_NE(Zone(kHub), nullptr);
}

TEST_F(WorldPartitionRuntimeTest, RedemandResetsLingerClock)
{
    LoadFixture(WorldPartitionStreamingConfig{
        .HopCount = 0,
        .LingerSeconds = 3.0,
    });
    Partition.SetFocus(kHub);
    Partition.PinZone(kHallway, ZoneParticipation{});
    Step();

    Partition.UnpinZone(kHallway);
    Step(2.0);
    ASSERT_NE(Zone(kHallway), nullptr);

    Partition.PinZone(kHallway, ZoneParticipation{});
    Step();
    Partition.UnpinZone(kHallway);
    Step(2.0);
    EXPECT_NE(Zone(kHallway), nullptr);
    Step(1.0);
    EXPECT_EQ(Zone(kHallway), nullptr);
}

TEST_F(WorldPartitionRuntimeTest, UndemandedQueuedLoadCancelsBeforePackageBuild)
{
    LoadFixture(WorldPartitionStreamingConfig{ .HopCount = 0 });
    Partition.SetFocus(kHub);
    Partition.PinZone(kHallway, ZoneParticipation{});

    Partition.Update(0.0, Loader, RuntimeWorldState);
    ASSERT_TRUE(Loader.IsLoading(kHallway));

    Partition.UnpinZone(kHallway);
    Partition.Update(0.0, Loader, RuntimeWorldState);

    EXPECT_FALSE(Loader.IsLoading(kHallway));
    Tasks.PumpWork();
    Tasks.DrainCompletions();
    ProcessResidency();
    EXPECT_EQ(Zone(kHallway), nullptr);
}

TEST_F(WorldPartitionRuntimeTest, ParticipationLeasesComposeByUnionAndReleaseIndependently)
{
    LoadFixture(WorldPartitionStreamingConfig{
        .HopCount = 0,
        .LingerSeconds = 30.0,
    });
    Partition.SetFocus(kHub);

    const ParticipationLeaseId logic =
        Partition.AcquireParticipationLease(
            kHallway,
            ZoneParticipation{ .Logic = true });
    const ParticipationLeaseId audio =
        Partition.AcquireParticipationLease(
            kHallway,
            ZoneParticipation{ .Audio = true });

    EXPECT_TRUE(Partition.IsParticipationLeaseValid(logic));
    EXPECT_TRUE(Partition.IsParticipationLeaseValid(audio));
    EXPECT_EQ(Partition.ParticipationLeaseCount(), 2u);

    Step();
    Step();

    ASSERT_NE(Zone(kHallway), nullptr);
    EXPECT_TRUE(Zone(kHallway)->Participation.Logic);
    EXPECT_TRUE(Zone(kHallway)->Participation.Audio);

    ASSERT_TRUE(Partition.ReleaseParticipationLease(logic));
    Step();
    EXPECT_FALSE(Zone(kHallway)->Participation.Logic);
    EXPECT_TRUE(Zone(kHallway)->Participation.Audio);
    EXPECT_FALSE(Partition.IsParticipationLeaseValid(logic));
    EXPECT_TRUE(Partition.IsParticipationLeaseValid(audio));

    ASSERT_TRUE(Partition.ReleaseParticipationLease(audio));
    Step();
    EXPECT_FALSE(Zone(kHallway)->Participation.Logic);
    EXPECT_FALSE(Zone(kHallway)->Participation.Audio);
    EXPECT_EQ(Partition.ParticipationLeaseCount(), 0u);
}

TEST_F(WorldPartitionRuntimeTest, ForcedInvalidationMakesLeaseTokensStale)
{
    LoadFixture();
    const ParticipationLeaseId first =
        Partition.AcquireParticipationLease(
            kHallway,
            ZoneParticipation{ .Physics = true });
    const ParticipationLeaseId second =
        Partition.AcquireParticipationLease(
            kHallway,
            ZoneParticipation{ .Logic = true });

    EXPECT_EQ(Partition.InvalidateParticipationLeases(kHallway), 2u);
    EXPECT_FALSE(Partition.IsParticipationLeaseValid(first));
    EXPECT_FALSE(Partition.IsParticipationLeaseValid(second));
    EXPECT_FALSE(Partition.ReleaseParticipationLease(first));
    EXPECT_FALSE(Partition.ReleaseParticipationLease(second));
    EXPECT_EQ(Partition.ParticipationLeaseCount(), 0u);
}

TEST_F(WorldPartitionRuntimeTest, ManifestReloadInvalidatesOutstandingLeases)
{
    LoadFixture();
    const ParticipationLeaseId lease =
        Partition.AcquireParticipationLease(
            kArena,
            ZoneParticipation{ .Logic = true });
    ASSERT_TRUE(Partition.IsParticipationLeaseValid(lease));

    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(FixtureManifest(), &error))
        << error;

    EXPECT_FALSE(Partition.IsParticipationLeaseValid(lease));
    EXPECT_EQ(Partition.ParticipationLeaseCount(), 0u);
}

TEST_F(WorldPartitionRuntimeTest, DemandRecordsRemainDeterministicallySorted)
{
    LoadFixture();
    Partition.SetFocus(kHallway);
    Step();
    Step();

    std::vector<ZoneId> first;
    for (const ZoneDemandRecord& record : Partition.DemandRecords())
        first.push_back(record.Zone);

    Partition.Update(0.0, Loader, RuntimeWorldState);
    RuntimeWorldState.FlushLifecycleRequests();

    std::vector<ZoneId> second;
    for (const ZoneDemandRecord& record : Partition.DemandRecords())
        second.push_back(record.Zone);

    EXPECT_EQ(first, second);
    ASSERT_GE(first.size(), 3u);
    for (std::size_t index = 1; index < first.size(); ++index)
        EXPECT_LT(first[index - 1].Value, first[index].Value);
}

TEST_F(WorldPartitionRuntimeTest, OverlappingZoneAabbsAreLegal)
{
    // Hallway's bounds stretched to overlap Hub over x in [7, 8].
    WorldPartitionManifest manifest = FixtureManifest();
    manifest.Zones[1].Bounds = Aabb3d::FromMinMax(
        Vec3d{ 7, 0, -2 },
        Vec3d{ 20, 4, 2 });
    std::string error;
    EXPECT_TRUE(Partition.LoadManifest(std::move(manifest), &error)) << error;
}

TEST_F(WorldPartitionRuntimeTest, ExplicitRelocationInNoZoneFocusesNearestZone)
{
    LoadFixture();
    Partition.SetFocus(kHub);
    // Far east of every bounds: the nearest zone (Arena, x up to 40) wins;
    // the stale previous focus does not survive.
    Partition.RelocateFocus(Vec3d{ 1000.0, 0.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kArena);
    // Hovering above the Hub's box focuses the Hub: the floor-slab case
    // (derived bounds hug geometry; the pawn transform rides above them).
    Partition.RelocateFocus(Vec3d{ 0.0, 10.0, 0.0 });
    EXPECT_EQ(Partition.FocusZone(), kHub);
}

TEST_F(WorldPartitionRuntimeTest, CoincidentZoneAabbsUseDeterministicAndPreferredResolution)
{
    WorldPartitionManifest tie = FixtureManifest();
    tie.Zones[1].Bounds = tie.Zones[0].Bounds;
    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(std::move(tie), &error)) << error;
    const ZoneContainmentResult deterministic = Partition.ResolveZoneAt(Vec3d{}, {});
    EXPECT_TRUE(deterministic.Ambiguous);
    EXPECT_EQ(deterministic.Chosen, kHub);
    EXPECT_EQ(Partition.ResolveZoneAt(Vec3d{}, kHallway).Chosen, kHallway);
}

TEST(WorldPartitionRuntimeThreaded, MidBuildCancellationRetriesThenDetaches)
{
    LoggingProvider logging;
    AsyncTaskQueue tasks(1);
    const WorldComponentSchema schema = EmptySchema();
    RuntimeWorld runtimeWorld(schema);
    ComponentSerializerRegistry serializers;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop frameLoop;
    AsyncZoneLoader loader(
        tasks,
        runtimeWorld,
        schema,
        serializers,
        sceneContext,
        frameLoop);

    std::atomic<bool> started{ false };
    std::atomic<bool> release{ false };
    WorldPartitionRuntime partition(
        [&](const ZoneHeader& header)
        {
            ZoneLoadRecipe recipe;
            if (header.Id == kHallway)
            {
                recipe.Build = [&](ZoneLoadPackage& package)
                {
                    started = true;
                    while (!release)
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1));
                    }
                    package.CreateEntity();
                };
            }
            else
            {
                recipe.Build = [](ZoneLoadPackage& package)
                {
                    package.CreateEntity();
                };
            }
            return recipe;
        },
        WorldPartitionStreamingConfig{
            .HopCount = 0,
            .LingerSeconds = 0.0,
        });

    std::string error;
    ASSERT_TRUE(partition.LoadManifest(FixtureManifest(), &error))
        << error;
    partition.SetFocus(kHub);
    partition.PinZone(kHallway, ZoneParticipation{});
    partition.Update(0.0, loader, runtimeWorld);

    const auto deadline =
        std::chrono::steady_clock::now()
        + std::chrono::seconds(10);
    while (!started)
    {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    partition.UnpinZone(kHallway);
    partition.Update(0.0, loader, runtimeWorld);
    EXPECT_TRUE(loader.IsLoading(kHallway));

    bool reportedLingering = false;
    for (const ZoneDemandRecord& record : partition.DemandRecords())
    {
        if (record.Zone == kHallway)
            reportedLingering = record.Sources.Lingering;
    }
    EXPECT_TRUE(reportedLingering);

    release = true;
    while (loader.IsLoading(kHallway))
    {
        tasks.DrainCompletions();
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    while (runtimeWorld.FindZone(kHallway) != nullptr)
    {
        partition.Update(0.1, loader, runtimeWorld);
        runtimeWorld.FlushLifecycleRequests();
        tasks.DrainCompletions();
        runtimeWorld.FlushLifecycleRequests();
        (void)runtimeWorld.BeginResidencyProcessing();
        runtimeWorld.FinalizeResidencyProcessing();
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(runtimeWorld.FindZone(kHallway), nullptr);
}

// Lease slots are recycled, so the generation is what stops an old token from
// acting on the lease that took its index. WorldPartitionRuntime.h states the
// guarantee outright: a stale token never releases a reused slot.
TEST_F(WorldPartitionRuntimeTest, AStaleTokenCannotReleaseAReusedSlot)
{
    LoadFixture();

    const ParticipationLeaseId first = Partition.AcquireParticipationLease(
        kHallway, ZoneParticipation{ .Physics = true });
    ASSERT_TRUE(Partition.ReleaseParticipationLease(first));

    const ParticipationLeaseId second = Partition.AcquireParticipationLease(
        kHallway, ZoneParticipation{ .Logic = true });
    ASSERT_EQ(first.Index, second.Index) << "the slot was not recycled, so this "
                                            "does not test aliasing";
    EXPECT_NE(first.Generation, second.Generation);

    EXPECT_FALSE(Partition.IsParticipationLeaseValid(first));
    EXPECT_TRUE(Partition.IsParticipationLeaseValid(second));
    EXPECT_FALSE(Partition.ReleaseParticipationLease(first))
        << "a stale token released the lease that reused its slot";
    EXPECT_TRUE(Partition.IsParticipationLeaseValid(second));
    EXPECT_EQ(Partition.ParticipationLeaseCount(), 1u);
}
