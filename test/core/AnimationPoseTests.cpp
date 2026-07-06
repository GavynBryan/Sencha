#include <anim/AnimationBlend.h>
#include <anim/AnimationClipSampler.h>
#include <anim/AnimationPoseStore.h>
#include <anim/SkeletonPose.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    void ExpectVecNear(const Vec3d& actual, const Vec3d& expected, float epsilon = 1e-5f)
    {
        EXPECT_NEAR(actual.X, expected.X, epsilon);
        EXPECT_NEAR(actual.Y, expected.Y, epsilon);
        EXPECT_NEAR(actual.Z, expected.Z, epsilon);
    }

    void ExpectQuatNear(const Quat<float>& actual, const Quat<float>& expected, float epsilon = 1e-5f)
    {
        EXPECT_NEAR(actual.X, expected.X, epsilon);
        EXPECT_NEAR(actual.Y, expected.Y, epsilon);
        EXPECT_NEAR(actual.Z, expected.Z, epsilon);
        EXPECT_NEAR(actual.W, expected.W, epsilon);
    }

    SkeletonData MakeTwoJointSkeleton()
    {
        SkeletonData skeleton;
        SkeletonJoint root;
        root.BindTranslation = Vec3d(1.0f, 0.0f, 0.0f);
        root.BindRotation = Quat<float>::Identity();
        root.BindScale = Vec3d(1.0f, 1.0f, 1.0f);
        root.InverseBind = Mat4::MakeTranslation(-1.0f, 0.0f, 0.0f);
        skeleton.Joints.push_back(root);

        SkeletonJoint child;
        child.ParentIndex = 0;
        child.BindTranslation = Vec3d(0.0f, 2.0f, 0.0f);
        child.BindRotation = Quat<float>::Identity();
        child.BindScale = Vec3d(1.0f, 1.0f, 1.0f);
        child.InverseBind = Mat4::MakeTranslation(-1.0f, -2.0f, 0.0f);
        skeleton.Joints.push_back(child);
        return skeleton;
    }
} // namespace

TEST(AnimationPoseStore, ReusesFreedSlotsByJointCountInLifoOrder)
{
    AnimationPoseStore store;
    const AnimationPoseHandle a = store.Allocate(2);
    const AnimationPoseHandle b = store.Allocate(3);
    const AnimationPoseHandle c = store.Allocate(2);

    store.Free(a);
    store.Free(b);
    store.Free(c);

    const AnimationPoseHandle reusedTwo = store.Allocate(2);
    EXPECT_EQ(reusedTwo.Index, c.Index);
    EXPECT_NE(reusedTwo.Generation, c.Generation);

    const AnimationPoseHandle reusedThree = store.Allocate(3);
    EXPECT_EQ(reusedThree.Index, b.Index);
    EXPECT_NE(reusedThree.Generation, b.Generation);
}

TEST(AnimationClipSampler, WritesBindPoseAndLeavesUntrackedChannels)
{
    SkeletonData skeleton = MakeTwoJointSkeleton();
    std::vector<JointTransform> pose(skeleton.Joints.size());
    WriteBindPose(skeleton, pose);

    ExpectVecNear(pose[0].Translation, Vec3d(1.0f, 0.0f, 0.0f));
    ExpectVecNear(pose[1].Translation, Vec3d(0.0f, 2.0f, 0.0f));

    AnimationClipData clip;
    clip.DurationSeconds = 1.0f;
    AnimationJointTrack track;
    track.JointIndex = 0;
    track.Path = AnimationChannelPath::Translation;
    track.Interpolation = AnimationInterpolation::Linear;
    track.TimesSeconds = { 0.0f, 1.0f };
    track.Values = { 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f };
    clip.Tracks.push_back(track);

    SampleClip(clip, 0.25f, pose);
    ExpectVecNear(pose[0].Translation, Vec3d(2.5f, 0.0f, 0.0f));
    ExpectVecNear(pose[1].Translation, Vec3d(0.0f, 2.0f, 0.0f));
}

TEST(AnimationClipSampler, StepSamplingUsesPreviousKeyAtBoundary)
{
    std::vector<JointTransform> pose(1);
    AnimationClipData clip;
    clip.DurationSeconds = 2.0f;
    AnimationJointTrack track;
    track.JointIndex = 0;
    track.Path = AnimationChannelPath::Translation;
    track.Interpolation = AnimationInterpolation::Step;
    track.TimesSeconds = { 0.0f, 1.0f, 2.0f };
    track.Values = { 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, 9.0f, 0.0f, 0.0f };
    clip.Tracks.push_back(track);

    SampleClip(clip, 0.999f, pose);
    ExpectVecNear(pose[0].Translation, Vec3d(0.0f, 0.0f, 0.0f));
    SampleClip(clip, 1.0f, pose);
    ExpectVecNear(pose[0].Translation, Vec3d(5.0f, 0.0f, 0.0f));
}

TEST(AnimationClipSampler, NlerpUsesShortestQuaternionHemisphere)
{
    std::vector<JointTransform> pose(1);
    AnimationClipData clip;
    clip.DurationSeconds = 1.0f;
    AnimationJointTrack track;
    track.JointIndex = 0;
    track.Path = AnimationChannelPath::Rotation;
    track.Interpolation = AnimationInterpolation::Linear;
    track.TimesSeconds = { 0.0f, 1.0f };
    track.Values = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f };
    clip.Tracks.push_back(track);

    SampleClip(clip, 0.5f, pose);
    ExpectQuatNear(pose[0].Rotation, Quat<float>::Identity());
}

TEST(AnimationClipSampler, WrapClipTimeHandlesLoopingAndClamping)
{
    EXPECT_FLOAT_EQ(WrapClipTime(2.5f, 2.0f, true), 0.5f);
    EXPECT_FLOAT_EQ(WrapClipTime(-0.25f, 2.0f, true), 1.75f);
    EXPECT_FLOAT_EQ(WrapClipTime(3.0f, 2.0f, false), 2.0f);
    EXPECT_FLOAT_EQ(WrapClipTime(-1.0f, 2.0f, false), 0.0f);
}

TEST(AnimationBlend, BlendsAndMasksPoses)
{
    std::vector<JointTransform> a(2);
    std::vector<JointTransform> b(2);
    std::vector<JointTransform> out(2);
    a[0].Translation = Vec3d(0.0f, 0.0f, 0.0f);
    b[0].Translation = Vec3d(10.0f, 0.0f, 0.0f);
    a[1].Translation = Vec3d(0.0f, 0.0f, 0.0f);
    b[1].Translation = Vec3d(0.0f, 10.0f, 0.0f);

    BlendPoses(a, b, 0.5f, out);
    ExpectVecNear(out[0].Translation, Vec3d(5.0f, 0.0f, 0.0f));
    ExpectVecNear(out[1].Translation, Vec3d(0.0f, 5.0f, 0.0f));

    const float weights[] = { 1.0f, 0.25f };
    BlendPosesMasked(a, b, 0.5f, weights, out);
    ExpectVecNear(out[0].Translation, Vec3d(5.0f, 0.0f, 0.0f));
    ExpectVecNear(out[1].Translation, Vec3d(0.0f, 1.25f, 0.0f));
}

TEST(AnimationBlend, WeightedBlendUsesCookedOrder)
{
    std::vector<JointTransform> a(1);
    std::vector<JointTransform> b(1);
    std::vector<JointTransform> out(1);
    a[0].Translation = Vec3d(0.0f, 0.0f, 0.0f);
    b[0].Translation = Vec3d(10.0f, 0.0f, 0.0f);

    const std::span<const JointTransform> poseSpans[] = { a, b };
    const float weights[] = { 0.25f, 0.75f };
    BlendPosesWeighted(poseSpans, weights, out);

    ExpectVecNear(out[0].Translation, Vec3d(7.5f, 0.0f, 0.0f));
}

TEST(SkeletonPose, BuildsIdentityPaletteForBindPose)
{
    SkeletonData skeleton = MakeTwoJointSkeleton();
    std::vector<JointTransform> pose(skeleton.Joints.size());
    std::vector<Mat4> scratch(skeleton.Joints.size());
    std::vector<Mat4> palette(skeleton.Joints.size());

    WriteBindPose(skeleton, pose);
    BuildSkinningPalette(skeleton, pose, scratch, palette);

    EXPECT_TRUE(palette[0].NearlyEquals(Mat4::Identity()));
    EXPECT_TRUE(palette[1].NearlyEquals(Mat4::Identity()));
}
