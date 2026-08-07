#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "document/commands/EditWorldManifestCommand.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

// Structural world edits (adding and renaming graphs and zones, moving a zone
// between graphs, retuning streaming) are undoable like any other edit. Undo
// also has to put the session back somewhere valid: rolling back the zone the
// user is standing in cannot leave the focus pointing at a zone that is gone.
namespace
{

class WorldManifestUndoTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override { World.NewWorld("ManifestUndoTest"); }

    [[nodiscard]] std::size_t ZoneCount() { return World.Manifest().Zones.size(); }
    [[nodiscard]] std::size_t GraphCount() { return World.Manifest().Graphs.size(); }

    [[nodiscard]] GraphId FirstGraph() { return World.Manifest().Graphs.front().Id; }

    [[nodiscard]] const ZoneHeader* Zone(ZoneId id)
    {
        for (const ZoneHeader& zone : World.Manifest().Zones)
            if (zone.Id == id)
                return &zone;
        return nullptr;
    }

    // The panel's recording shape, exercised directly.
    template <typename Fn>
    void Edit(Fn&& verb)
    {
        WorldPartitionManifest before = World.Manifest();
        if (!verb())
            return;
        RecordManifestEdit(World, Commands, std::move(before));
    }

    LoggingProvider Logging;
    CommandStack Commands;
    WorldDocument World{ Logging };
};

TEST_F(WorldManifestUndoTest, AddingAZoneUndoesAndRedoes)
{
    const std::size_t before = ZoneCount();
    Edit([&] { return World.AddZone(FirstGraph(), "Added").IsValid(); });
    ASSERT_EQ(ZoneCount(), before + 1);

    Commands.Undo();
    EXPECT_EQ(ZoneCount(), before);

    Commands.Redo();
    EXPECT_EQ(ZoneCount(), before + 1);
}

TEST_F(WorldManifestUndoTest, AddingAGraphUndoes)
{
    const std::size_t before = GraphCount();
    Edit([&] { return World.AddGraph("Added").IsValid(); });
    ASSERT_EQ(GraphCount(), before + 1);

    Commands.Undo();
    EXPECT_EQ(GraphCount(), before);
}

TEST_F(WorldManifestUndoTest, RenamingAZoneUndoesToTheOldName)
{
    const ZoneId zone = World.Manifest().Zones.front().Id;
    const std::string original(Zone(zone)->Name);

    Edit([&] { return World.RenameZone(zone, "Renamed"); });
    ASSERT_EQ(Zone(zone)->Name, "Renamed");

    Commands.Undo();
    EXPECT_EQ(Zone(zone)->Name, original);
}

TEST_F(WorldManifestUndoTest, MovingAZoneBetweenGraphsUndoes)
{
    const ZoneId zone = World.Manifest().Zones.front().Id;
    const GraphId original = Zone(zone)->Graph;
    GraphId second;
    Edit([&] { second = World.AddGraph("Second"); return second.IsValid(); });
    ASSERT_TRUE(second.IsValid());

    Edit([&] { return World.SetZoneGraph(zone, second); });
    ASSERT_EQ(Zone(zone)->Graph, second);

    Commands.Undo();
    EXPECT_EQ(Zone(zone)->Graph, original);
}

TEST_F(WorldManifestUndoTest, StreamingTuningUndoes)
{
    const GraphId graph = FirstGraph();
    Edit([&] { return World.SetGraphHopCount(graph, 3); });
    Edit([&] { return World.SetGraphResidentCap(graph, 7); });

    Commands.Undo(); // the cap
    Commands.Undo(); // the hop count

    const GraphRecord& record = World.Manifest().Graphs.front();
    EXPECT_FALSE(record.Streaming.HopCount.has_value());
    EXPECT_FALSE(record.Streaming.ResidentZoneCap.has_value());
}

TEST_F(WorldManifestUndoTest, UndoingTheFocusedZonesCreationReFocusesSomewhereValid)
{
    GraphId graph = FirstGraph();
    ZoneId added;
    Edit([&] { added = World.AddZone(graph, "Added"); return added.IsValid(); });
    ASSERT_TRUE(World.SetFocusZone(added));
    ASSERT_EQ(World.FocusZone(), added);

    Commands.Undo();

    // The focused zone no longer exists, so the focus moved rather than
    // dangling; whatever it landed on is a zone the manifest still describes.
    EXPECT_NE(World.FocusZone(), added);
    if (World.FocusZone().IsValid())
    {
        EXPECT_NE(Zone(World.FocusZone()), nullptr);
    }
    EXPECT_FALSE(World.IsZoneOpen(added));
}

TEST_F(WorldManifestUndoTest, ARefusedVerbRecordsNoUndoStep)
{
    // Renaming a zone that does not exist changes nothing, so nothing is
    // recorded and an unrelated earlier edit stays at the top of the stack.
    Edit([&] { return World.RenameZone(World.Manifest().Zones.front().Id, "Kept"); });
    const std::size_t stackDepthProxy = ZoneCount();

    Edit([&] { return World.RenameZone(ZoneId{}, "Nope"); });

    Commands.Undo(); // must undo the rename, not a phantom step
    EXPECT_EQ(World.Manifest().Zones.front().Name, "Zone 1");
    EXPECT_EQ(ZoneCount(), stackDepthProxy);
}

}
