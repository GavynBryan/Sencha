#include <gtest/gtest.h>

#include <graphics/RenderFrameHealth.h>
#include <render/SkinnedPoseFrameData.h>

#include <vector>

namespace
{
std::vector<BufferHandle> AllPosed(std::size_t count)
{
    std::vector<BufferHandle> posed(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        posed[index].Index = static_cast<std::uint32_t>(index + 1);
        posed[index].Generation = 1;
    }
    return posed;
}
}

TEST(SkinnedPoseOutcome, CountsEveryVisibleInstanceAsPosedOrAtRest)
{
    std::vector<BufferHandle> posed = AllPosed(3);
    posed[1] = {};

    const SkinnedPoseOutcome outcome = CountSkinnedPoseOutcome(posed);

    EXPECT_EQ(outcome.Visible, 3u);
    EXPECT_EQ(outcome.Posed, 2u);
    EXPECT_EQ(outcome.AtRest, 1u);
}

TEST(SkinnedPoseOutcome, NothingVisibleIsNothingLost)
{
    const SkinnedPoseOutcome outcome = CountSkinnedPoseOutcome({});

    EXPECT_EQ(outcome.Visible, 0u);
    EXPECT_EQ(outcome.AtRest, 0u);
}

TEST(SkinnedPoseOutcome, InstancesPastTheRecordedCountDrawAtRest)
{
    std::vector<BufferHandle> posed = AllPosed(65);

    InvalidateUnrecordedPoses(posed, 64);

    const SkinnedPoseOutcome outcome = CountSkinnedPoseOutcome(posed);
    EXPECT_EQ(outcome.Posed, 64u);
    EXPECT_EQ(outcome.AtRest, 1u);
    EXPECT_FALSE(posed[64].IsValid());
    EXPECT_TRUE(posed[63].IsValid());
}

TEST(SkinnedPoseOutcome, TheRecordedCountSkipsInstancesThatNeverHadABuffer)
{
    std::vector<BufferHandle> posed = AllPosed(4);
    posed[0] = {};

    InvalidateUnrecordedPoses(posed, 2);

    EXPECT_FALSE(posed[0].IsValid());
    EXPECT_TRUE(posed[1].IsValid());
    EXPECT_TRUE(posed[2].IsValid());
    EXPECT_FALSE(posed[3].IsValid());
}

TEST(RenderFrameHealth, ACleanFrameLostNothing)
{
    EXPECT_FALSE(RenderFrameHealth{}.LostWork());
}

TEST(RenderFrameHealth, EveryKindOfLossCounts)
{
    EXPECT_TRUE(RenderFrameHealth{ .ScratchFailures = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .PassesSkipped = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .InstancesDropped = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .ShadowCastersDropped = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .SkinnedAtRest = 1 }.LostWork());
}
