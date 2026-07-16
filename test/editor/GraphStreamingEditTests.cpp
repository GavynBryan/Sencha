#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/WorldDocument.h"
#include "ui/WorldPartitionPanel.h"

#include <core/logging/LoggingProvider.h>

namespace
{

// A fresh world (one graph, one zone) for the graph streaming verbs.
class GraphStreamingEditTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("TestWorld");
        Graph = World.Manifest().Graphs[0].Id;
    }

    LoggingProvider Logging;   // sink-less: silent
    WorldDocument World{ Logging };
    GraphId Graph;
};

} // namespace

TEST_F(GraphStreamingEditTest, VerbsRewriteAndRevalidate)
{
    ASSERT_TRUE(World.SetGraphHopCount(Graph, 2));
    ASSERT_TRUE(World.SetGraphRadius(Graph, 250.0));
    ASSERT_TRUE(World.SetGraphResidentCap(Graph, 12));

    const GraphStreamingConfig& streaming = World.Manifest().Graphs[0].Streaming;
    EXPECT_EQ(streaming.HopCount, 2);
    EXPECT_EQ(streaming.Radius, 250.0);
    EXPECT_EQ(streaming.ResidentZoneCap, 12);
    EXPECT_TRUE(World.IsDirty());

    // An invalid value lands in the manifest and validation reports it; the
    // verb rewrites, the rules judge.
    ASSERT_TRUE(World.SetGraphResidentCap(Graph, 0));
    bool reported = false;
    for (const ContentRiskRecord& record : World.ValidationRecords())
        reported |= record.RuleId == "partition.graph.streaming_invalid";
    EXPECT_TRUE(reported);
}

TEST_F(GraphStreamingEditTest, ClearingReturnsFieldsToInherited)
{
    ASSERT_TRUE(World.SetGraphHopCount(Graph, 2));
    ASSERT_TRUE(World.SetGraphRadius(Graph, 250.0));
    ASSERT_TRUE(World.SetGraphResidentCap(Graph, 12));

    ASSERT_TRUE(World.SetGraphHopCount(Graph, std::nullopt));
    ASSERT_TRUE(World.SetGraphRadius(Graph, std::nullopt));
    ASSERT_TRUE(World.SetGraphResidentCap(Graph, std::nullopt));

    EXPECT_EQ(World.Manifest().Graphs[0].Streaming, GraphStreamingConfig{});
}

TEST_F(GraphStreamingEditTest, VerbsRefuseUnknownGraph)
{
    EXPECT_FALSE(World.SetGraphHopCount(GraphId{ 0xdead }, 1));
    EXPECT_FALSE(World.SetGraphRadius(GraphId{ 0xdead }, 1.0));
    EXPECT_FALSE(World.SetGraphResidentCap(GraphId{ 0xdead }, 1));
}

TEST(GraphStreamingShapeLabel, DerivesShapeFromRadiusInForce)
{
    GraphStreamingConfig streaming;
    EXPECT_STREQ(GraphStreamingShapeLabel(streaming, 0.0), "Inherited (Graph)");
    EXPECT_STREQ(GraphStreamingShapeLabel(streaming, 250.0), "Inherited (Proximity)");

    streaming.Radius = 0.0;   // explicit zero pins the graph shape
    EXPECT_STREQ(GraphStreamingShapeLabel(streaming, 250.0), "Graph");

    streaming.Radius = 250.0;
    EXPECT_STREQ(GraphStreamingShapeLabel(streaming, 0.0), "Proximity");

    streaming.HopCount = 2;   // hop and cap overrides never change the shape
    streaming.ResidentZoneCap = 12;
    EXPECT_STREQ(GraphStreamingShapeLabel(streaming, 0.0), "Proximity");
}
