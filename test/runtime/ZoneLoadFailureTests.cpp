// A zone load that refuses must be recorded and withheld from reissue.
//
// Demand cannot express this on its own: a demanded, non-resident zone looks
// identical to one that will never load, so a policy that reissues purely from
// demand rebuilds a broken zone every frame. These bound that behavior and the
// recovery path that lifts it.

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
#include <world/build/EntityBuildPackage.h>

#include "ZoneDockFixture.h"

#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace
{
constexpr ZoneId kHub{ 0xa1 };
constexpr ZoneId kHallway{ 0xa2 };

// Two zones, one edge, so a hop of 1 demands both and a refusal on one can be
// shown not to affect the other.
std::string ManifestJson(std::uint64_t hubHash)
{
    return
        R"({"format_version":1,"name":"FailureFixture","start_zone":"00000000000000a1",)"
        R"("regions":[{"id":"00000000000000b1","name":"R"}],"zones":[)"
        R"({"id":"00000000000000a1","name":"Hub","region":"00000000000000b1",)"
        R"("scene":"levels/hub.sscene",)"
        R"("bounds":{"min":[-8,0,-8],"max":[8,4,8]},)"
        R"("cooked_scene":"levels/hub.smap",)"
        R"("content_hash":")" + [hubHash]
        {
            char buffer[17];
            std::snprintf(buffer, sizeof(buffer), "%016lx",
                          static_cast<unsigned long>(hubHash));
            return std::string(buffer);
        }() + R"("},)"
        R"({"id":"00000000000000a2","name":"Hallway","region":"00000000000000b1",)"
        R"("scene":"levels/hall.sscene",)"
        R"("bounds":{"min":[9,0,-2],"max":[20,4,2]},)"
        R"("cooked_scene":"levels/hall.smap",)"
        R"("content_hash":"00000000000000d2"})"
        R"(]})";
}

WorldPartitionManifest Manifest(std::uint64_t hubHash = 0xd1)
{
    const std::optional<JsonValue> json = JsonParse(ManifestJson(hubHash));
    EXPECT_TRUE(json.has_value());
    std::string error;
    std::optional<WorldPartitionManifest> manifest =
        ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    AddFixtureDockPair(*manifest, 0xc1, kHub, kHallway, Vec3d{ 8.5, 2, 0 });
    return *manifest;
}

WorldComponentSchema EmptySchema()
{
    WorldComponentSchema schema;
    schema.Seal();
    return schema;
}

// Drives the real streaming path with a recipe whose per-zone outcome the test
// controls, counting how often each zone's content is rebuilt.
class ZoneLoadFailureTest : public ::testing::Test
{
protected:
    ZoneLoadFailureTest()
        : Tasks(0)
        , Schema(EmptySchema())
        , World_(Schema)
        , SceneContext(Logging)
        , Loader(Tasks, World_, Schema, Serializers, SceneContext, FrameLoop)
        , Partition(MakeRecipe(), Config())
    {
    }

    static WorldPartitionStreamingConfig Config()
    {
        WorldPartitionStreamingConfig config;
        config.HopCount = 1;
        config.LingerSeconds = 1000.0;  // never unload during these tests
        return config;
    }

    ZoneLoadRecipeFn MakeRecipe()
    {
        return [this](const ZoneHeader& header)
        {
            const ZoneId zone = header.Id;
            ZoneLoadRecipe recipe;
            recipe.Build = [this, zone](EntityBuildPackage& package)
            {
                ++Builds[zone.Value];
                package.CreateEntity();
            };
            recipe.Finalize = [this, zone](RuntimeWorld&, RuntimeZoneRecord&)
            {
                ++Finalizes[zone.Value];
                return Refuse.find(zone.Value) == Refuse.end();
            };
            return recipe;
        };
    }

    void LoadManifest(std::uint64_t hubHash = 0xd1)
    {
        std::string error;
        ASSERT_TRUE(Partition.LoadManifest(Manifest(hubHash), &error)) << error;
        Partition.SetFocus(kHub);
    }

    void Step(int frames = 1)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            Partition.Update(1.0 / 60.0, Loader, World_);
            World_.FlushLifecycleRequests();
            Tasks.PumpWork();
            Tasks.DrainCompletions();
            World_.FlushLifecycleRequests();
            (void)World_.BeginResidencyProcessing();
            World_.FinalizeResidencyProcessing();
        }
    }

    int BuildsOf(ZoneId zone) { return Builds[zone.Value]; }

    LoggingProvider Logging;
    AsyncTaskQueue Tasks;
    WorldComponentSchema Schema;
    RuntimeWorld World_;
    ComponentSerializerRegistry Serializers;
    SceneSerializationContext SceneContext;
    RuntimeFrameLoop FrameLoop;
    AsyncZoneLoader Loader;
    WorldPartitionRuntime Partition;
    std::map<std::uint64_t, int> Builds;
    std::map<std::uint64_t, int> Finalizes;
    std::set<std::uint64_t> Refuse;
};
} // namespace

// The defect this file exists for: before suppression, a refusing zone was
// rebuilt and re-imported once per frame, forever, with nothing recorded.
TEST_F(ZoneLoadFailureTest, ARefusedZoneIsBuiltOnceNotEveryFrame)
{
    Refuse.insert(kHub.Value);
    Refuse.insert(kHallway.Value);
    LoadManifest();

    Step(30);

    EXPECT_EQ(BuildsOf(kHub), 1)
        << "the hub was rebuilt " << BuildsOf(kHub)
        << " times over 30 frames: a refused load is being reissued from demand";
    EXPECT_EQ(BuildsOf(kHallway), 1) << "neighbour rebuilt "
                                     << BuildsOf(kHallway) << " times";
    EXPECT_EQ(World_.ZoneCount(), 0u);
}

// The refusal must be observable. A game gating a loading screen on residency
// otherwise waits forever while the loader churns.
TEST_F(ZoneLoadFailureTest, ARefusalIsRecordedWithItsStage)
{
    Refuse.insert(kHub.Value);
    LoadManifest();

    Step(3);

    const ZoneLoadFailure* failure = Loader.FindFailure(kHub);
    ASSERT_NE(failure, nullptr) << "the refusal was dropped";
    EXPECT_EQ(failure->Zone, kHub);
    EXPECT_EQ(failure->Stage, ZoneLoadStage::Finalize);
    EXPECT_FALSE(failure->Message.empty());
    EXPECT_TRUE(Partition.IsZoneLoadSuppressed(kHub));
}

// Suppression is per zone. One broken zone must not stall a healthy neighbour.
TEST_F(ZoneLoadFailureTest, AHealthyZoneStillLoadsBesideARefusedOne)
{
    Refuse.insert(kHub.Value);
    LoadManifest();

    Step(5);

    EXPECT_TRUE(Partition.IsZoneLoadSuppressed(kHub));
    EXPECT_FALSE(Partition.IsZoneLoadSuppressed(kHallway));
    EXPECT_TRUE(World_.IsZoneResident(kHallway))
        << "a refusal on the focus zone blocked its neighbour";
    EXPECT_EQ(BuildsOf(kHallway), 1);
}

// Recovery: recooked content is different content, so the old refusal no longer
// describes it and the zone becomes eligible again without a restart.
TEST_F(ZoneLoadFailureTest, ChangedContentHashLiftsSuppression)
{
    Refuse.insert(kHub.Value);
    LoadManifest(0xd1);
    Step(5);
    ASSERT_TRUE(Partition.IsZoneLoadSuppressed(kHub));
    ASSERT_EQ(BuildsOf(kHub), 1);

    // The zone is recooked and now loads.
    Refuse.erase(kHub.Value);
    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(Manifest(0xd9), &error)) << error;
    Partition.SetFocus(kHub);

    Step(5);

    EXPECT_FALSE(Partition.IsZoneLoadSuppressed(kHub));
    EXPECT_EQ(Loader.FindFailure(kHub), nullptr) << "the stale record survived";
    EXPECT_TRUE(World_.IsZoneResident(kHub));
    EXPECT_EQ(BuildsOf(kHub), 2) << "the recooked zone was never retried";
}

// Identical content after a manifest reload is the same broken content, so the
// refusal must still hold. Otherwise a reload becomes a retry loop by proxy.
TEST_F(ZoneLoadFailureTest, ReloadingIdenticalContentKeepsSuppression)
{
    Refuse.insert(kHub.Value);
    LoadManifest(0xd1);
    Step(5);
    ASSERT_EQ(BuildsOf(kHub), 1);

    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(Manifest(0xd1), &error)) << error;
    Partition.SetFocus(kHub);
    Step(5);

    EXPECT_TRUE(Partition.IsZoneLoadSuppressed(kHub));
    EXPECT_EQ(BuildsOf(kHub), 1)
        << "reloading the same content retried a zone known to refuse it";
}

// A zone that refuses once and is later fixed in place must not stay suppressed
// by a stale record after it successfully publishes.
TEST_F(ZoneLoadFailureTest, ASuccessfulLoadClearsAnEarlierRefusal)
{
    Refuse.insert(kHub.Value);
    LoadManifest();
    Step(3);
    ASSERT_NE(Loader.FindFailure(kHub), nullptr);

    // Clearing suppression is the caller's signal that the content is worth
    // another attempt; the successful publish must retire the record.
    Refuse.erase(kHub.Value);
    ASSERT_TRUE(Loader.ClearFailure(kHub));
    ASSERT_TRUE(World_.IsZoneResident(kHallway));

    // Suppression in the runtime is keyed on content, so lift it the same way a
    // recook would.
    std::string error;
    ASSERT_TRUE(Partition.LoadManifest(Manifest(0xdd), &error)) << error;
    Partition.SetFocus(kHub);
    Step(5);

    EXPECT_TRUE(World_.IsZoneResident(kHub));
    EXPECT_EQ(Loader.FindFailure(kHub), nullptr);
    EXPECT_EQ(Partition.SuppressedLoadCount(), 0u);
}

// Repeated refusals of the same zone coalesce: one record, not a growing list.
TEST_F(ZoneLoadFailureTest, RepeatedRefusalsCoalesceToOneRecord)
{
    Refuse.insert(kHub.Value);
    Refuse.insert(kHallway.Value);
    LoadManifest();

    Step(20);

    EXPECT_EQ(Loader.Failures().size(), 2u)
        << "failure records accumulated per attempt instead of per zone";
    EXPECT_EQ(Partition.SuppressedLoadCount(), 2u);
}
