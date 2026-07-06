#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"

#include <core/logging/LoggingProvider.h>

#include <string_view>

namespace
{

// A world with two zones (From, To) for exercising the transition verbs.
class TransitionValidationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("TestWorld");
        FromZone = World.Manifest().Zones[0].Id;
        ToZone = World.AddZone(World.Manifest().Regions[0].Id, "To");
    }

    LoggingProvider Logging;   // sink-less: silent
    WorldDocument World{ Logging };
    ZoneId FromZone;
    ZoneId ToZone;
};

} // namespace

TEST_F(TransitionValidationTest, AddTransitionMintsAndReindexes)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(id.IsValid());
    ASSERT_EQ(World.Index().Outgoing(FromZone).size(), 1u);
    EXPECT_EQ(World.Manifest().Transitions[World.Index().Outgoing(FromZone)[0]].Id, id);
    EXPECT_TRUE(World.IsDirty());
}

TEST_F(TransitionValidationTest, RemoveTransitionDropsRecordAndRevalidates)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(World.RemoveTransition(id));

    EXPECT_TRUE(World.Manifest().Transitions.empty());
    EXPECT_TRUE(World.Index().Outgoing(FromZone).empty());
    EXPECT_FALSE(World.RemoveTransition(id));
}

TEST_F(TransitionValidationTest, SettersRewriteAndRevalidate)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(World.SetTransitionTopology(id, TransitionTopology::Teleport));
    EXPECT_EQ(World.Manifest().Transitions[0].Topology, TransitionTopology::Teleport);

    ASSERT_TRUE(World.SetTransitionOneWay(id, true));
    EXPECT_TRUE(World.Manifest().Transitions[0].Flags.OneWay);

    ASSERT_TRUE(World.SetTransitionPreloadPriority(id, 7));
    EXPECT_EQ(World.Manifest().Transitions[0].PreloadPriority, 7);

    const TransitionId unknown{ 0x00000000000000ffull };
    EXPECT_FALSE(World.SetTransitionTopology(unknown, TransitionTopology::Seam));
    EXPECT_FALSE(World.SetTransitionOneWay(unknown, true));
    EXPECT_FALSE(World.SetTransitionPreloadPriority(unknown, 1));
}

TEST_F(TransitionValidationTest, RenameTransitionRewritesAndRevalidates)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(World.RenameTransition(id, "Front Door"));
    EXPECT_EQ(World.Manifest().Transitions[0].Name, "Front Door");
    EXPECT_TRUE(World.IsDirty());

    // Clearing restores the derived display label (empty stored name).
    ASSERT_TRUE(World.RenameTransition(id, ""));
    EXPECT_TRUE(World.Manifest().Transitions[0].Name.empty());
    EXPECT_FALSE(World.RenameTransition(TransitionId{ 0xff }, "x"));
}

TEST_F(TransitionValidationTest, TagAndDepthVerbsRewriteRecords)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(World.SetTransitionRequiredTags(id, { "quest.bridge", "power.on" }));
    ASSERT_EQ(World.Manifest().Transitions[0].RequiredTags.size(), 2u);
    EXPECT_EQ(World.Manifest().Transitions[0].RequiredTags[0], "quest.bridge");

    ASSERT_TRUE(World.SetTransitionPreloadDepth(id, 3));
    EXPECT_EQ(World.Manifest().Transitions[0].PreloadDepth, 3);
    ASSERT_TRUE(World.SetTransitionPreloadDepth(id, -5));
    EXPECT_EQ(World.Manifest().Transitions[0].PreloadDepth, 0);   // clamped

    const TransitionId unknown{ 0xff };
    EXPECT_FALSE(World.SetTransitionRequiredTags(unknown, {}));
    EXPECT_FALSE(World.SetTransitionPreloadDepth(unknown, 1));
}
