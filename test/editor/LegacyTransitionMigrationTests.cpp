#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"

#include <core/logging/LoggingProvider.h>
#include <zone/WorldConnectionComponents.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

TransitionId AddLegacyTransition(WorldDocument& world, ZoneId from, ZoneId to,
                                 TransitionTopology topology, bool oneWay)
{
    TransitionRecord transition;
    transition.Id = TransitionId{ 0xc1 + world.Manifest().Transitions.size() };
    transition.From = from;
    transition.To = to;
    transition.Topology = topology;
    transition.Flags.OneWay = oneWay;
    world.Manifest().Transitions.push_back(std::move(transition));
    return world.Manifest().Transitions.back().Id;
}

class LegacyTransitionMigrationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = std::filesystem::temp_directory_path()
            / ("sencha_transition_migration_"
               + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(Root);
        std::filesystem::create_directories(Root);
        World.NewWorld("MigrationWorld");
        ZoneA = World.FocusZone();
        ZoneB = World.AddZone(World.Manifest().Graphs[0].Id, "Zone B");
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    LoggingProvider Logging;
    WorldDocument World{ Logging };
    ZoneId ZoneA;
    ZoneId ZoneB;
    std::filesystem::path Root;
};

} // namespace

TEST_F(LegacyTransitionMigrationTest, ReciprocalTeleportsCollapseIntoOneWorldLink)
{
    // A reciprocal pair: A->B and B->A.
    AddLegacyTransition(World, ZoneA, ZoneB, TransitionTopology::Teleport, false);
    AddLegacyTransition(World, ZoneB, ZoneA, TransitionTopology::Teleport, false);

    const LegacyTransitionMigrationReport report = World.MigrateLegacyTransitions();

    ASSERT_EQ(report.Links.size(), 1u);
    EXPECT_EQ(report.CollapsedPairs, 1u);
    EXPECT_TRUE(report.Unresolved.empty());
    EXPECT_TRUE(World.Manifest().Transitions.empty());
    const EditorScene& scene = World.WorldSceneDocument().GetScene();
    ASSERT_EQ(scene.GetEntityCount(), 1u);
    const WorldLink* link = scene.GetRegistry().Components.TryGet<WorldLink>(
        scene.GetAllEntities()[0]);
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE((link->ZoneA == ZoneA && link->ZoneB == ZoneB)
                || (link->ZoneA == ZoneB && link->ZoneB == ZoneA));
    EXPECT_EQ(link->Directions, DockDirectionBoth);
}

TEST_F(LegacyTransitionMigrationTest, GeometricTransitionRemainsUnresolved)
{
    const TransitionId doorway = AddLegacyTransition(
        World, ZoneA, ZoneB, TransitionTopology::Doorway, true);

    const LegacyTransitionMigrationReport report = World.MigrateLegacyTransitions();

    EXPECT_TRUE(report.Links.empty());
    ASSERT_EQ(report.Unresolved.size(), 1u);
    EXPECT_EQ(report.Unresolved[0], doorway);
    ASSERT_EQ(World.Manifest().Transitions.size(), 1u);
    EXPECT_EQ(World.Manifest().Transitions[0].Id, doorway);
    EXPECT_EQ(World.WorldSceneDocument().GetScene().GetEntityCount(), 0u);
}

TEST_F(LegacyTransitionMigrationTest,
       ExplicitTeleportConversionCollapsesReciprocalGeometricRows)
{
    const TransitionId forward = AddLegacyTransition(
        World, ZoneA, ZoneB, TransitionTopology::Doorway, false);
    AddLegacyTransition(
        World, ZoneB, ZoneA, TransitionTopology::Doorway, false);

    EXPECT_TRUE(World.ConvertLegacyTransitionToTeleport(forward));
    EXPECT_TRUE(World.Manifest().Transitions.empty());
    const EditorScene& scene = World.WorldSceneDocument().GetScene();
    ASSERT_EQ(scene.GetEntityCount(), 1u);
    const WorldLink* link = scene.GetRegistry().Components.TryGet<WorldLink>(
        scene.GetAllEntities()[0]);
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->ZoneA, ZoneA);
    EXPECT_EQ(link->ZoneB, ZoneB);
    EXPECT_EQ(link->Kind, LinkKind::Teleport);
    EXPECT_EQ(link->Directions, DockDirectionBoth);
}

TEST_F(LegacyTransitionMigrationTest, NewFormatSaveRefusesUnresolvedLegacyRecords)
{
    const TransitionId doorway = AddLegacyTransition(
        World, ZoneA, ZoneB, TransitionTopology::Doorway, true);
    const auto path = Root / "migration.sworld";

    EXPECT_FALSE(World.SaveWorldAs(path.string()));
    EXPECT_TRUE(World.DiscardLegacyTransition(doorway));
    EXPECT_TRUE(World.SaveWorld());

}
