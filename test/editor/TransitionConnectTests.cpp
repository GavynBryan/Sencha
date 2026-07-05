#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/TransitionConnect.h"
#include "document/WorldDocument.h"

#include <core/logging/LoggingProvider.h>

#include <string_view>

namespace
{

// Two zones ready to connect.
class TransitionConnectTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("TestWorld");
        FromZone = World.Manifest().Zones[0].Id;
        ToZone = World.AddZone(World.Manifest().Regions[0].Id, "To");
    }

    [[nodiscard]] size_t CountRecords(std::string_view ruleId) const
    {
        size_t count = 0;
        for (const ContentRiskRecord& record : World.ValidationRecords())
            if (record.RuleId == ruleId)
                ++count;
        return count;
    }

    LoggingProvider Logging;   // sink-less: silent
    WorldDocument World{ Logging };
    ZoneId FromZone;
    ZoneId ToZone;
};

} // namespace

TEST_F(TransitionConnectTest, ConnectMintsDoorwayPairWithDefaults)
{
    const TransitionId forward = ConnectZones(World, FromZone, ToZone, /*oneWay*/ false);

    ASSERT_TRUE(forward.IsValid());
    ASSERT_EQ(World.Manifest().Transitions.size(), 2u);
    const TransitionRecord& first = World.Manifest().Transitions[0];
    const TransitionRecord& second = World.Manifest().Transitions[1];
    EXPECT_EQ(first.Id, forward);
    EXPECT_EQ(first.From, FromZone);
    EXPECT_EQ(first.To, ToZone);
    EXPECT_EQ(second.From, ToZone);
    EXPECT_EQ(second.To, FromZone);
    for (const TransitionRecord& record : World.Manifest().Transitions)
    {
        EXPECT_EQ(record.Topology, TransitionTopology::Doorway);
        EXPECT_FALSE(record.Flags.OneWay);
        EXPECT_EQ(record.PreloadPriority, 0);
    }
    // The pair is symmetric by construction: no unpaired warning.
    EXPECT_EQ(CountRecords("partition.transition.unpaired"), 0u);
}

TEST_F(TransitionConnectTest, OneWayMintsSingleEdge)
{
    const TransitionId forward = ConnectZones(World, FromZone, ToZone, /*oneWay*/ true);

    ASSERT_TRUE(forward.IsValid());
    ASSERT_EQ(World.Manifest().Transitions.size(), 1u);
    EXPECT_TRUE(World.Manifest().Transitions[0].Flags.OneWay);
}

TEST_F(TransitionConnectTest, RefusesSelfAndUnknownZones)
{
    EXPECT_FALSE(ConnectZones(World, FromZone, FromZone, false).IsValid());
    EXPECT_FALSE(ConnectZones(World, FromZone, ZoneId{ 0xff }, false).IsValid());
    EXPECT_FALSE(ConnectZones(World, ZoneId{ 0xff }, ToZone, false).IsValid());
    EXPECT_TRUE(World.Manifest().Transitions.empty());
}
