#include <gtest/gtest.h>

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/TransitionConnect.h"
#include "document/WorldDocument.h"

#include <core/logging/LoggingProvider.h>

#include <string_view>

namespace
{

// Two zones with a portal brush in the focus zone, ready to connect.
class TransitionConnectTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("TestWorld");
        FromZone = World.Manifest().Zones[0].Id;
        ToZone = World.AddZone(World.Manifest().Regions[0].Id, "To");

        EditorScene& scene = World.FocusDocument().GetScene();
        Portal = scene.CreateBrush(Vec3d{ 8, 0, 0 }, Vec3d{ 4.0, 4.0, 0.2 });
        scene.GetRegistry().Components.AddComponent(Portal, PortalComponent{});
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
    CommandStack Stack;
    ZoneId FromZone;
    ZoneId ToZone;
    EntityId Portal;
};

} // namespace

TEST_F(TransitionConnectTest, ConnectMintsDoorwayPairWithDefaults)
{
    const TransitionId forward =
        ConnectZones(World, FromZone, ToZone, /*oneWay*/ false, EntityId{}, Stack);

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
    // The derived pair is symmetric by construction: no unpaired warning.
    EXPECT_EQ(CountRecords("partition.transition.unpaired"), 0u);
}

TEST_F(TransitionConnectTest, OneWayMintsSingleEdge)
{
    const TransitionId forward =
        ConnectZones(World, FromZone, ToZone, /*oneWay*/ true, EntityId{}, Stack);

    ASSERT_TRUE(forward.IsValid());
    ASSERT_EQ(World.Manifest().Transitions.size(), 1u);
    EXPECT_TRUE(World.Manifest().Transitions[0].Flags.OneWay);
}

TEST_F(TransitionConnectTest, PortalLinksUndoably)
{
    const TransitionId forward =
        ConnectZones(World, FromZone, ToZone, /*oneWay*/ false, Portal, Stack);

    const EditorScene& scene = World.FocusDocument().GetScene();
    ASSERT_EQ(scene.TryGetPortal(Portal)->Transition, forward);
    // One linked portal satisfies the symmetric pair: both directions clear.
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
    EXPECT_EQ(CountRecords("partition.transition.portal_unverified"), 0u);

    // The link is the undoable half; the manifest edges are verbs and stay.
    ASSERT_TRUE(Stack.CanUndo());
    Stack.Undo();
    EXPECT_FALSE(scene.TryGetPortal(Portal)->Transition.IsValid());
    EXPECT_EQ(World.Manifest().Transitions.size(), 2u);

    Stack.Redo();
    EXPECT_EQ(scene.TryGetPortal(Portal)->Transition, forward);
}

TEST_F(TransitionConnectTest, RefusesSelfAndUnknownZones)
{
    EXPECT_FALSE(ConnectZones(World, FromZone, FromZone, false, EntityId{}, Stack).IsValid());
    EXPECT_FALSE(
        ConnectZones(World, FromZone, ZoneId{ 0xff }, false, EntityId{}, Stack).IsValid());
    EXPECT_FALSE(
        ConnectZones(World, ZoneId{ 0xff }, ToZone, false, EntityId{}, Stack).IsValid());
    EXPECT_TRUE(World.Manifest().Transitions.empty());
}

TEST_F(TransitionConnectTest, NonPortalEntityIsNotLinked)
{
    EditorScene& scene = World.FocusDocument().GetScene();
    const EntityId plain = scene.CreateBrush(Vec3d{ -8, 0, 0 });

    const TransitionId forward =
        ConnectZones(World, FromZone, ToZone, /*oneWay*/ false, plain, Stack);

    ASSERT_TRUE(forward.IsValid());
    EXPECT_FALSE(Stack.CanUndo());   // nothing linked, nothing on the stack
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 1u);
}
