#include <gtest/gtest.h>

#include <graphics/RenderFrameHealth.h>
#include <render/SkinnedPoseFrameData.h>

TEST(SkinnedPoseOutcome, EveryStateCountsTowardVisible)
{
    const SkinnedPoseOutcome outcome{ .FirstPose = 1, .Refreshed = 2, .Held = 3, .AwaitingFirstPose = 4, .BindFallback = 5 };

    EXPECT_EQ(outcome.Visible(), 15u);
}

TEST(SkinnedPoseOutcome, FailuresAreNotAnExtraState)
{
    const SkinnedPoseOutcome outcome{ .Held = 2, .PoseFailures = 2 };

    EXPECT_EQ(outcome.Visible(), 2u);
}

TEST(RenderFrameHealth, ACleanFrameLostNothing)
{
    EXPECT_FALSE(RenderFrameHealth{}.LostWork());
}

TEST(RenderFrameHealth, HoldingAPoseOverBudgetIsDegradationNotLoss)
{
    EXPECT_FALSE(RenderFrameHealth{ .SkinnedHeld = 128 }.LostWork());
    EXPECT_FALSE(RenderFrameHealth{ .SkinnedAwaitingFirstPose = 16 }.LostWork());
}

TEST(RenderFrameHealth, EveryKindOfLossCounts)
{
    EXPECT_TRUE(RenderFrameHealth{ .ScratchFailures = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .PassesSkipped = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .InstancesDropped = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .ShadowCastersDropped = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .SkinnedBindFallback = 1 }.LostWork());
    EXPECT_TRUE(RenderFrameHealth{ .SkinnedPoseFailures = 1 }.LostWork());
}
