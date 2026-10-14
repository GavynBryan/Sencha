#include "document/DocumentSerialization.h"
#include "document/EditorEntityRecipe.h"
#include "document/WorldDocument.h"
#include "document/ZoneGraph.h"

#include <core/logging/LoggingProvider.h>
#include <zone/WorldConnectionComponents.h>

#include <gtest/gtest.h>

// A world's zones as a graph: each dock in the world scene is an edge between
// the centres of the zones it joins, carrying which ways it can be crossed and
// what the world's validation says about it.
namespace
{
class ZoneGraphTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("GraphWorld");
        ZoneA = World.FocusZone();
        ZoneB = World.AddZone(World.Manifest().Graphs[0].Id, "Zone B");
        ASSERT_TRUE(World.SetZoneBounds(ZoneA, Aabb3d::FromMinMax({ -5, -5, 0 }, { 5, 5, 10 })));
        ASSERT_TRUE(World.SetZoneBounds(ZoneB, Aabb3d::FromMinMax({ -5, -5, -10 }, { 5, 5, 0 })));
    }

    WorldDock& AddDock()
    {
        const EditorCreateContext context{ .World = &World,
                                           .ActiveZone = ZoneA,
                                           .ActiveGraph = World.Manifest().Graphs[0].Id,
                                           .PlacementPoint = {},
                                           .PlacementNormal = Vec3d::Forward() };
        const EntityId entity = World.WorldSceneDocument().RestoreEntity(WorldDockRecipe{}.Build(context));
        WorldDock* dock = World.WorldSceneDocument().GetScene().GetRegistry().Components.TryGet<WorldDock>(entity);
        EXPECT_NE(dock, nullptr);
        dock->ZoneA = ZoneA;
        dock->ZoneB = ZoneB;
        return *dock;
    }

    LoggingProvider Logging;
    WorldDocument World{ Logging };
    ZoneId ZoneA;
    ZoneId ZoneB;
};
} // namespace

TEST_F(ZoneGraphTest, ADockIsAnEdgeBetweenTheCentresOfItsZones)
{
    WorldDock& dock = AddDock();
    dock.Directions = DockDirectionAToB;
    const std::vector<ZoneGraphEdge> edges = ZoneGraphEdges(World);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].Connection, ZoneGraphConnection::Dock);
    EXPECT_EQ(edges[0].From, Vec3d(0, 0, 5));
    EXPECT_EQ(edges[0].To, Vec3d(0, 0, -5));
    EXPECT_TRUE(edges[0].AToB);
    EXPECT_FALSE(edges[0].BToA);
    EXPECT_FALSE(edges[0].CrossGraph);
}

TEST_F(ZoneGraphTest, AnEdgeCarriesWhatValidationSaysAboutItsDock)
{
    WorldDock& dock = AddDock();
    dock.HalfExtents.X = 0.0f;
    World.Revalidate();
    const std::vector<ZoneGraphEdge> edges = ZoneGraphEdges(World);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_TRUE(edges[0].Warned || edges[0].Failed) << "a degenerate plane is reported";
}

TEST_F(ZoneGraphTest, ADockToAZoneWithoutBoundsIsNoEdge)
{
    WorldDock& dock = AddDock();
    dock.ZoneB = ZoneId{ 0xdead };
    EXPECT_TRUE(ZoneGraphEdges(World).empty());
}
