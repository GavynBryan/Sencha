// Simulation-side debug lines reach render form through extraction alone, and
// a channel keeps its lines until its producer replaces them.

#include <debug/DebugLines.h>
#include <ecs/World.h>
#include <render/extract/DebugLineExtraction.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

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

namespace
{
    struct Extent
    {
        Vec3d Min{ 1e9f, 1e9f, 1e9f };
        Vec3d Max{ -1e9f, -1e9f, -1e9f };
    };

    Extent Bounds(const DebugLineList& lines)
    {
        Extent extent;
        for (const DebugLine& line : lines.Tested())
        {
            for (const Vec3d& point : { line.From, line.To })
            {
                extent.Min = Vec3d(std::min(extent.Min.X, point.X), std::min(extent.Min.Y, point.Y),
                                   std::min(extent.Min.Z, point.Z));
                extent.Max = Vec3d(std::max(extent.Max.X, point.X), std::max(extent.Max.Y, point.Y),
                                   std::max(extent.Max.Z, point.Z));
            }
        }
        return extent;
    }
}

TEST(DebugLines, ANarrowConeStaysInsideItsAngleAndRange)
{
    DebugLineList lines;
    const float halfAngle = 0.5f;
    AppendCone(lines, Vec3d::Zero(), Vec3d(0, 0, -1), halfAngle, 10.0f, Vec4(1, 1, 1, 1));
    ASSERT_FALSE(lines.Tested().empty());
    float widest = 0.0f;
    for (const DebugLine& line : lines.Tested())
    {
        for (const Vec3d& point : { line.From, line.To })
        {
            const float distance = point.Magnitude();
            EXPECT_LE(distance, 10.0f + 1e-3f);
            if (distance > 1e-3f)
            {
                const float angle = std::acos(std::clamp(-point.Z / distance, -1.0f, 1.0f));
                EXPECT_LE(angle, halfAngle + 1e-3f);
                widest = std::max(widest, angle);
            }
        }
    }
    EXPECT_NEAR(widest, halfAngle, 1e-3f);
}

TEST(DebugLines, AnAllRoundSensorDrawsASphereNotALine)
{
    DebugLineList lines;
    AppendCone(lines, Vec3d::Zero(), Vec3d(0, 0, -1), std::numbers::pi_v<float>, 10.0f, Vec4(1, 1, 1, 1));
    const Extent extent = Bounds(lines);
    for (const float low : { extent.Min.X, extent.Min.Y, extent.Min.Z })
        EXPECT_NEAR(low, -10.0f, 0.2f);
    for (const float high : { extent.Max.X, extent.Max.Y, extent.Max.Z })
        EXPECT_NEAR(high, 10.0f, 0.2f);
}

TEST(DebugLines, AWideConeReachesBehindTheApex)
{
    DebugLineList lines;
    AppendCone(lines, Vec3d::Zero(), Vec3d(0, 0, -1), 2.5f, 10.0f, Vec4(1, 1, 1, 1));
    const Extent extent = Bounds(lines);
    EXPECT_GT(extent.Max.Z, 7.0f);
    EXPECT_NEAR(extent.Max.X, 10.0f, 0.2f);
}
