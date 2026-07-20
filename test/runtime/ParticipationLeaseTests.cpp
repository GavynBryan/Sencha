#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionManifestJson.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneRuntime.h>

namespace
{
constexpr ZoneId kFocus{ 0xa1 };
constexpr ZoneId kLeased{ 0xa2 };

constexpr const char* kManifestJson = R"({
  "format_version": 1,
  "name": "LeaseFixture",
  "start_zone": "00000000000000a1",
  "regions": [ { "id": "00000000000000b1", "name": "Fixture" } ],
  "zones": [
    { "id": "00000000000000a1", "name": "Focus", "region": "00000000000000b1",
      "scene": "focus.level.json",
      "bounds": { "min": [0, 0, 0], "max": [4, 4, 4] },
      "cooked_scene": "focus.cooked.json" },
    { "id": "00000000000000a2", "name": "Leased", "region": "00000000000000b1",
      "scene": "leased.level.json",
      "bounds": { "min": [10, 0, 0], "max": [14, 4, 4] },
      "cooked_scene": "leased.cooked.json" }
  ],
  "transitions": []
})";

WorldPartitionManifest LeaseManifest()
{
    const auto json = JsonParse(kManifestJson);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const auto manifest = ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    return *manifest;
}

const ZoneDemandRecord* FindDemand(
    std::span<const ZoneDemandRecord> records,
    ZoneId zone)
{
    for (const ZoneDemandRecord& record : records)
        if (record.Zone == zone)
            return &record;
    return nullptr;
}

struct LeaseFixture
{
    LeaseFixture()
        : Tasks(0)
        , Loader(Tasks, Zones, Runtime)
        , Partition(
            [](const ZoneHeader&) {
                ZoneLoadRecipe recipe;
                recipe.Build = [](Registry&) {};
                return recipe;
            },
            WorldPartitionStreamingConfig{ .HopCount = 0, .LingerSeconds = 0.0 })
    {
        std::string error;
        EXPECT_TRUE(Partition.LoadManifest(LeaseManifest(), &error)) << error;
        Partition.SetFocus(kFocus);
    }

    void Update()
    {
        Partition.Update(0.0, Loader, Zones);
    }

    AsyncTaskQueue Tasks;
    ZoneRuntime Zones;
    RuntimeFrameLoop Runtime;
    AsyncZoneLoader Loader;
    WorldPartitionRuntime Partition;
};
} // namespace

TEST(ParticipationLease, IndependentHoldersComposeAndReleaseIndependently)
{
    LeaseFixture fixture;

    const ParticipationLeaseId physics = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Physics = true });
    const ParticipationLeaseId logic = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Logic = true });

    ASSERT_TRUE(physics.IsValid());
    ASSERT_TRUE(logic.IsValid());
    EXPECT_EQ(fixture.Partition.ParticipationLeaseCount(), 2u);

    fixture.Update();
    const ZoneDemandRecord* combined =
        FindDemand(fixture.Partition.DemandRecords(), kLeased);
    ASSERT_NE(combined, nullptr);
    EXPECT_TRUE(combined->Desired.Physics);
    EXPECT_TRUE(combined->Desired.Logic);

    EXPECT_TRUE(fixture.Partition.ReleaseParticipationLease(physics));
    fixture.Update();

    const ZoneDemandRecord* logicOnly =
        FindDemand(fixture.Partition.DemandRecords(), kLeased);
    ASSERT_NE(logicOnly, nullptr);
    EXPECT_FALSE(logicOnly->Desired.Physics);
    EXPECT_TRUE(logicOnly->Desired.Logic);

    EXPECT_TRUE(fixture.Partition.ReleaseParticipationLease(logic));
    fixture.Update();
    EXPECT_EQ(FindDemand(fixture.Partition.DemandRecords(), kLeased), nullptr);
    EXPECT_EQ(fixture.Partition.ParticipationLeaseCount(), 0u);
}

TEST(ParticipationLease, StaleTokenCannotReleaseReusedSlot)
{
    LeaseFixture fixture;

    const ParticipationLeaseId first = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Physics = true });
    ASSERT_TRUE(fixture.Partition.ReleaseParticipationLease(first));

    const ParticipationLeaseId second = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Logic = true });
    ASSERT_EQ(first.Index, second.Index);
    EXPECT_NE(first.Generation, second.Generation);

    EXPECT_FALSE(fixture.Partition.IsParticipationLeaseValid(first));
    EXPECT_TRUE(fixture.Partition.IsParticipationLeaseValid(second));
    EXPECT_FALSE(fixture.Partition.ReleaseParticipationLease(first));
    EXPECT_TRUE(fixture.Partition.IsParticipationLeaseValid(second));
}

TEST(ParticipationLease, ForcedTeardownInvalidatesAllZoneHolders)
{
    LeaseFixture fixture;

    const ParticipationLeaseId physics = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Physics = true });
    const ParticipationLeaseId logic = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Logic = true });

    EXPECT_EQ(fixture.Partition.InvalidateParticipationLeases(kLeased), 2u);
    EXPECT_FALSE(fixture.Partition.IsParticipationLeaseValid(physics));
    EXPECT_FALSE(fixture.Partition.IsParticipationLeaseValid(logic));
    EXPECT_FALSE(fixture.Partition.ReleaseParticipationLease(physics));
    EXPECT_EQ(fixture.Partition.ParticipationLeaseCount(), 0u);
}

TEST(ParticipationLease, ManifestReloadInvalidatesOutstandingTokens)
{
    LeaseFixture fixture;

    const ParticipationLeaseId lease = fixture.Partition.AcquireParticipationLease(
        kLeased, ZoneParticipation{ .Physics = true });
    ASSERT_TRUE(fixture.Partition.IsParticipationLeaseValid(lease));

    std::string error;
    ASSERT_TRUE(fixture.Partition.LoadManifest(LeaseManifest(), &error)) << error;

    EXPECT_FALSE(fixture.Partition.IsParticipationLeaseValid(lease));
    EXPECT_FALSE(fixture.Partition.ReleaseParticipationLease(lease));
    EXPECT_EQ(fixture.Partition.ParticipationLeaseCount(), 0u);
}
