#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "ui/WorldPartitionPanel.h"

#include <core/logging/LoggingProvider.h>

namespace
{

// A fresh world (one region, one zone) for the region streaming verbs.
class RegionStreamingEditTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("TestWorld");
        Region = World.Manifest().Regions[0].Id;
    }

    LoggingProvider Logging;   // sink-less: silent
    WorldDocument World{ Logging };
    RegionId Region;
};

} // namespace

TEST_F(RegionStreamingEditTest, VerbsRewriteAndRevalidate)
{
    ASSERT_TRUE(World.SetRegionHopCount(Region, 2));
    ASSERT_TRUE(World.SetRegionRadius(Region, 250.0));
    ASSERT_TRUE(World.SetRegionResidentCap(Region, 12));

    const RegionStreamingConfig& streaming = World.Manifest().Regions[0].Streaming;
    EXPECT_EQ(streaming.HopCount, 2);
    EXPECT_EQ(streaming.Radius, 250.0);
    EXPECT_EQ(streaming.ResidentZoneCap, 12);
    EXPECT_TRUE(World.IsDirty());

    // An invalid value lands in the manifest and validation reports it; the
    // verb rewrites, the rules judge.
    ASSERT_TRUE(World.SetRegionResidentCap(Region, 0));
    bool reported = false;
    for (const ContentRiskRecord& record : World.ValidationRecords())
        reported |= record.RuleId == "partition.region.streaming_invalid";
    EXPECT_TRUE(reported);
}

TEST_F(RegionStreamingEditTest, ClearingReturnsFieldsToInherited)
{
    ASSERT_TRUE(World.SetRegionHopCount(Region, 2));
    ASSERT_TRUE(World.SetRegionRadius(Region, 250.0));
    ASSERT_TRUE(World.SetRegionResidentCap(Region, 12));

    ASSERT_TRUE(World.SetRegionHopCount(Region, std::nullopt));
    ASSERT_TRUE(World.SetRegionRadius(Region, std::nullopt));
    ASSERT_TRUE(World.SetRegionResidentCap(Region, std::nullopt));

    EXPECT_EQ(World.Manifest().Regions[0].Streaming, RegionStreamingConfig{});
}

TEST_F(RegionStreamingEditTest, VerbsRefuseUnknownRegion)
{
    EXPECT_FALSE(World.SetRegionHopCount(RegionId{ 0xdead }, 1));
    EXPECT_FALSE(World.SetRegionRadius(RegionId{ 0xdead }, 1.0));
    EXPECT_FALSE(World.SetRegionResidentCap(RegionId{ 0xdead }, 1));
}

TEST(RegionStreamingBadge, MapsOverrideStateToLabel)
{
    RegionStreamingConfig streaming;
    EXPECT_STREQ(RegionStreamingBadge(streaming), "Graph (inherited)");

    streaming.HopCount = 2;   // overridden, but still graph-shaped
    EXPECT_STREQ(RegionStreamingBadge(streaming), "Graph");

    streaming.Radius = 0.0;   // explicit zero radius is still graph
    EXPECT_STREQ(RegionStreamingBadge(streaming), "Graph");

    streaming.Radius = 250.0;
    EXPECT_STREQ(RegionStreamingBadge(streaming), "Radius");
}
