// Simulation-side debug lines reach render form through extraction alone, and
// a channel keeps its lines until its producer replaces them.

#include <debug/DebugLines.h>
#include <ecs/World.h>
#include <render/extract/DebugLineExtraction.h>

#include <gtest/gtest.h>

TEST(DebugLines, AChannelIsFoundByNameAndKeepsItsLinesUntilCleared)
{
    DebugLineChannels channels;
    const DebugLineChannelId first = channels.Channel("a");
    const DebugLineChannelId second = channels.Channel("b");
    EXPECT_NE(first, second);
    EXPECT_EQ(channels.Channel("a"), first);

    channels.Lines(first).AddTested(Vec3d::Zero(), Vec3d(1, 0, 0), Vec4(1, 0, 0, 1));
    EXPECT_FALSE(channels.Lines(first).Empty());
    EXPECT_TRUE(channels.Lines(second).Empty());
    channels.Lines(first).Clear();
    EXPECT_TRUE(channels.Lines(first).Empty());
}

TEST(DebugLines, ExtractionCopiesEveryChannelIntoVertexPairsByDepth)
{
    World world;
    DebugLineChannels& channels = world.AddResource<DebugLineChannels>();
    const Vec4 red(1, 0, 0, 1);
    channels.Lines(channels.Channel("a")).AddTested(Vec3d(0, 0, 0), Vec3d(1, 0, 0), red);
    channels.Lines(channels.Channel("b")).AddOnTop(Vec3d(0, 1, 0), Vec3d(0, 2, 0), red);
    channels.Lines(channels.Channel("b")).AddTested(Vec3d(0, 0, 1), Vec3d(0, 0, 2), red);

    DebugLineFrame frame;
    ExtractDebugLines(world, frame);
    ASSERT_EQ(frame.Tested.size(), 4u);
    ASSERT_EQ(frame.OnTop.size(), 2u);
    EXPECT_EQ(frame.Tested[1].Position, Vec3d(1, 0, 0));
    EXPECT_EQ(frame.OnTop[0].Position, Vec3d(0, 1, 0));
    EXPECT_EQ(frame.OnTop[0].Color, red);
}

TEST(DebugLines, AWorldWithNoChannelsExtractsNothing)
{
    World world;
    DebugLineFrame frame;
    ExtractDebugLines(world, frame);
    EXPECT_TRUE(frame.Tested.empty());
    EXPECT_TRUE(frame.OnTop.empty());
}

TEST(DebugLines, AConeDrawsItsRimAndFourSpokes)
{
    DebugLineList lines;
    AppendCone(lines, Vec3d::Zero(), Vec3d(0, 0, -1), 0.5f, 10.0f, Vec4(1, 1, 1, 1));
    EXPECT_EQ(lines.Tested().size(), 24u + 4u);
    for (const DebugLine& line : lines.Tested())
        EXPECT_LE(line.To.Z, 0.0f);
}
