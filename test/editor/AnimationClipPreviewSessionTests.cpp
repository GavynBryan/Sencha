#include "authoring/AnimationClipPreviewSession.h"

#include <gtest/gtest.h>

#include <limits>

namespace
{
SkeletonData Skeleton()
{
    SkeletonData skeleton;
    SkeletonJoint root;
    root.Name = "root";
    root.InverseBind = Mat4::Identity();
    skeleton.Joints.push_back(root);
    return skeleton;
}

AnimationClipData Clip(float duration = 1.0f)
{
    AnimationClipData clip;
    clip.SkeletonPath = "asset://preview.sskel";
    clip.DurationSeconds = duration;
    AnimationJointTrack track;
    track.TimesSeconds = { 0.0f, duration };
    track.Values = { 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f };
    clip.Tracks.push_back(track);
    return clip;
}

class AnimationClipPreviewTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(Session.SetContent("asset://preview.sskel", Skeleton(), Clip(), Error)) << Error;
    }
    AnimationClipPreviewSession Session;
    std::string Error;
};
}

TEST_F(AnimationClipPreviewTest, TickSteppingUsesRuntimeSampler)
{
    for (int i = 0; i < 15; ++i) Session.Step(1);
    EXPECT_EQ(Session.Tick(), 15u);
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), 0.25);
    ASSERT_EQ(Session.Palette().size(), 1u);
    EXPECT_NEAR(Session.Palette()[0].Data[0][3], 2.5f, 1e-5f);
    EXPECT_FALSE(Session.IsPlaying());
    Session.Step(-1);
    EXPECT_EQ(Session.Tick(), 14u);
}

TEST_F(AnimationClipPreviewTest, InspectionDoesNotChangePlaybackClock)
{
    Session.Step(1);
    Session.InspectNormalized(0.75);
    EXPECT_TRUE(Session.IsInspecting());
    EXPECT_EQ(Session.Tick(), 1u);
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), 0.75);
    EXPECT_NEAR(Session.Palette()[0].Data[0][3], 7.5f, 1e-5f);
    Session.Advance(0.1);
    EXPECT_EQ(Session.Tick(), 1u);
    Session.ReturnToPlayback();
    EXPECT_FALSE(Session.IsInspecting());
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), 1.0 / 60.0);
    EXPECT_NEAR(Session.Palette()[0].Data[0][3], 10.0f / 60.0f, 1e-5f);
}

TEST_F(AnimationClipPreviewTest, PlayReturnsFromInspectionWithoutSeekingPlayback)
{
    Session.Step(1);
    Session.InspectNormalized(1.0);
    Session.Play();
    EXPECT_FALSE(Session.IsInspecting());
    Session.Advance(1.0 / 60.0);
    EXPECT_EQ(Session.Tick(), 2u);
}

TEST_F(AnimationClipPreviewTest, WallFramePartitionDoesNotChangeTicks)
{
    AnimationClipPreviewSession other;
    ASSERT_TRUE(other.SetContent("asset://preview.sskel", Skeleton(), Clip(), Error));
    Session.Play();
    other.Play();
    for (int i = 0; i < 30; ++i) Session.Advance(1.0 / 120.0);
    other.Advance(0.25);
    EXPECT_EQ(Session.Tick(), 15u);
    EXPECT_EQ(Session.Tick(), other.Tick());
    EXPECT_EQ(Session.Palette(), other.Palette());
}

TEST_F(AnimationClipPreviewTest, SpeedChangesWallCadenceNotTickSize)
{
    ASSERT_TRUE(Session.SetSpeed(2.0));
    Session.Play();
    Session.Advance(0.1);
    EXPECT_EQ(Session.Tick(), 12u);
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), 0.2);
    Session.Step(1);
    EXPECT_EQ(Session.Tick(), 13u);
}

TEST_F(AnimationClipPreviewTest, LoopAndNonLoopHaveExplicitEndBehavior)
{
    Session.Play();
    for (int i = 0; i < 4; ++i) Session.Advance(0.25);
    EXPECT_EQ(Session.Tick(), 0u);
    EXPECT_TRUE(Session.IsPlaying());
    Session.SetLoop(false);
    for (int i = 0; i < 4; ++i) Session.Advance(0.25);
    EXPECT_EQ(Session.Tick(), 60u);
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), 1.0);
    EXPECT_FALSE(Session.IsPlaying());
    Session.Play();
    EXPECT_EQ(Session.Tick(), 0u);
}

TEST_F(AnimationClipPreviewTest, InspectionCanReachExactEndOfNonIntegralDuration)
{
    ASSERT_TRUE(Session.SetContent("asset://preview.sskel", Skeleton(), Clip(0.101f), Error));
    Session.SetLoop(false);
    Session.Play();
    Session.Advance(0.25);
    EXPECT_EQ(Session.Tick(), 7u);
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), static_cast<double>(0.101f));
    EXPECT_NEAR(Session.Palette()[0].Data[0][3], 10.0f, 1e-5f);
}

TEST_F(AnimationClipPreviewTest, InvalidReplacementPreservesPoseAndClock)
{
    Session.Step(1);
    Session.Play();
    const auto before = Session.Palette();
    auto clip = Clip();
    clip.SkeletonPath = "asset://other.sskel";
    EXPECT_FALSE(Session.SetContent("asset://preview.sskel", Skeleton(), clip, Error));
    EXPECT_FALSE(Error.empty());
    EXPECT_EQ(Session.Tick(), 1u);
    EXPECT_TRUE(Session.IsPlaying());
    EXPECT_EQ(Session.Palette(), before);
    clip = Clip();
    clip.Tracks.front().JointIndex = 1;
    EXPECT_FALSE(Session.SetContent("asset://preview.sskel", Skeleton(), clip, Error));
    EXPECT_EQ(Session.Palette(), before);
}

TEST_F(AnimationClipPreviewTest, InvalidSkeletonAndUnrepresentableDurationAreRejected)
{
    EXPECT_FALSE(Session.SetContent("asset://preview.sskel", {}, Clip(), Error));
    EXPECT_FALSE(Session.SetContent("not-an-asset", Skeleton(), std::nullopt, Error));
    auto clip = Clip();
    clip.DurationSeconds = std::numeric_limits<float>::max();
    EXPECT_FALSE(Session.SetContent("asset://preview.sskel", Skeleton(), clip, Error));
    EXPECT_EQ(Session.Duration(), 1.0);
}

TEST_F(AnimationClipPreviewTest, RepeatedSamplingReusesPoseStorage)
{
    const auto* storage = Session.Palette().data();
    for (int i = 0; i < 60; ++i)
    {
        Session.Step(1);
        EXPECT_EQ(Session.Palette().data(), storage);
    }
}

TEST_F(AnimationClipPreviewTest, ContentIsCapturedRatherThanBorrowed)
{
    auto clip = Clip();
    ASSERT_TRUE(Session.SetContent("asset://preview.sskel", Skeleton(), clip, Error));
    clip.Tracks[0].Values[3] = 99.0f;
    Session.InspectNormalized(1.0);
    EXPECT_NEAR(Session.Palette()[0].Data[0][3], 10.0f, 1e-5f);
}

TEST_F(AnimationClipPreviewTest, BindPoseAndEmptySessionAreSafe)
{
    ASSERT_TRUE(Session.SetContent("asset://preview.sskel", Skeleton(), std::nullopt, Error));
    Session.Play();
    Session.Advance(0.1);
    EXPECT_FALSE(Session.IsPlaying());
    EXPECT_EQ(Session.Tick(), 0u);
    EXPECT_EQ(Session.Palette()[0], Mat4::Identity());
    Session.Clear();
    EXPECT_TRUE(Session.Palette().empty());
    EXPECT_TRUE(Session.SkeletonPath().empty());
    Session.Step(-1);
    Session.Step(1);
    EXPECT_EQ(Session.Tick(), 0u);
}

TEST_F(AnimationClipPreviewTest, InvalidTransportInputsDoNotPoisonPose)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(Session.SetSpeed(nan));
    EXPECT_FALSE(Session.SetSpeed(-1.0));
    EXPECT_FALSE(Session.SetSpeed(0.0));
    EXPECT_FALSE(Session.SetSpeed(9.0));
    EXPECT_DOUBLE_EQ(Session.Speed(), 1.0);
    Session.Play();
    Session.Advance(nan);
    Session.Advance(-1.0);
    EXPECT_EQ(Session.Tick(), 0u);
    Session.InspectNormalized(nan);
    EXPECT_FALSE(Session.IsInspecting());
    Session.InspectNormalized(2.0);
    EXPECT_DOUBLE_EQ(Session.SampleSeconds(), 1.0);
}

TEST_F(AnimationClipPreviewTest, PauseRestartAndStepClearFractionalWallTime)
{
    Session.Play();
    Session.Advance(1.0 / 120.0);
    Session.Pause();
    Session.Play();
    Session.Advance(1.0 / 120.0);
    EXPECT_EQ(Session.Tick(), 0u);
    Session.Step(1);
    Session.Play();
    Session.Advance(1.0 / 120.0);
    EXPECT_EQ(Session.Tick(), 1u);
    Session.Restart();
    EXPECT_EQ(Session.Tick(), 0u);
    Session.Advance(1.0 / 120.0);
    EXPECT_EQ(Session.Tick(), 0u);
}
