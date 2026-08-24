// Sampling a cooked clip into a local pose: what a track overwrites, what it
// leaves at bind, how keys interpolate, and how the ends clamp. The pose then
// composes to model space and through the existing palette math, which is the
// path the pre-skin dispatch consumes.

#include <gtest/gtest.h>

#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>

#include <cmath>

namespace
{

// Two joints, child under root, deliberately not axis-aligned so an
// order-swapped compose cannot cancel out by accident.
SkeletonData MakeChain()
{
    SkeletonData skeleton;

    SkeletonJoint root;
    root.ParentIndex = -1;
    root.BindTranslation = Vec3d(1.0f, 2.0f, 3.0f);
    root.BindRotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.7f);
    skeleton.Joints.push_back(root);

    SkeletonJoint tip;
    tip.ParentIndex = 0;
    tip.BindTranslation = Vec3d(0.0f, 1.5f, 0.0f);
    skeleton.Joints.push_back(tip);

    std::vector<Mat4> model;
    BuildBindModelTransforms(skeleton, model);
    for (std::size_t joint = 0; joint < skeleton.Joints.size(); ++joint)
        skeleton.Joints[joint].InverseBind = model[joint].Inverse();
    return skeleton;
}

AnimationJointTrack MakeTranslationTrack(
    std::uint32_t joint, AnimationInterpolation interpolation,
    std::vector<float> times, std::vector<Vec3d> values)
{
    AnimationJointTrack track;
    track.JointIndex = joint;
    track.Path = AnimationChannelPath::Translation;
    track.Interpolation = interpolation;
    track.TimesSeconds = std::move(times);
    for (const Vec3d& value : values)
    {
        track.Values.push_back(value.X);
        track.Values.push_back(value.Y);
        track.Values.push_back(value.Z);
    }
    return track;
}

} // namespace

TEST(AnimationClipSampling, UnanimatedChannelsStayAtBind)
{
    const SkeletonData skeleton = MakeChain();

    // A clip that translates only the tip: the root keeps its whole bind
    // TRS, and the tip keeps the bind rotation and scale it does not
    // animate. This is what makes an unanimated skeleton identical to rest.
    AnimationClipData clip;
    clip.DurationSeconds = 1.0f;
    clip.Tracks.push_back(MakeTranslationTrack(
        1, AnimationInterpolation::Linear, { 0.0f, 1.0f },
        { Vec3d(0.0f, 1.5f, 0.0f), Vec3d(0.0f, 4.5f, 0.0f) }));

    std::vector<Transform3f> pose;
    SampleAnimationClip(clip, skeleton, 0.0f, pose);

    ASSERT_EQ(pose.size(), 2u);
    EXPECT_FLOAT_EQ(pose[0].Position.X, skeleton.Joints[0].BindTranslation.X);
    EXPECT_FLOAT_EQ(pose[0].Position.Y, skeleton.Joints[0].BindTranslation.Y);
    EXPECT_FLOAT_EQ(pose[0].Rotation.W, skeleton.Joints[0].BindRotation.W);
    EXPECT_FLOAT_EQ(pose[1].Scale.X, 1.0f);
    // At the first key the tip sits exactly where the track puts it, which
    // here is also its bind translation.
    EXPECT_FLOAT_EQ(pose[1].Position.Y, 1.5f);
}

TEST(AnimationClipSampling, KeysInterpolateHoldOrClampByMode)
{
    const SkeletonData skeleton = MakeChain();
    struct Case
    {
        const char* Why;
        AnimationInterpolation Mode;
        float Time;
        float ExpectedY;
    };
    const Case cases[] = {
        { "before the first key clamps to it", AnimationInterpolation::Linear,
          -5.0f, 0.0f },
        { "exactly on a key", AnimationInterpolation::Linear, 0.0f, 0.0f },
        { "midway interpolates", AnimationInterpolation::Linear, 1.0f, 10.0f },
        { "three quarters", AnimationInterpolation::Linear, 1.5f, 15.0f },
        { "after the last key clamps to it", AnimationInterpolation::Linear,
          99.0f, 20.0f },
        { "step holds the preceding key", AnimationInterpolation::Step,
          1.9f, 0.0f },
        { "step takes the next key exactly on it", AnimationInterpolation::Step,
          2.0f, 20.0f },
    };

    for (const Case& c : cases)
    {
        AnimationClipData clip;
        clip.DurationSeconds = 2.0f;
        clip.Tracks.push_back(MakeTranslationTrack(
            0, c.Mode, { 0.0f, 2.0f },
            { Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 20.0f, 0.0f) }));

        std::vector<Transform3f> pose;
        SampleAnimationClip(clip, skeleton, c.Time, pose);
        EXPECT_NEAR(pose[0].Position.Y, c.ExpectedY, 1e-5f) << c.Why;
    }
}

TEST(AnimationClipSampling, RotationTracksSlerpAndStayUnit)
{
    const SkeletonData skeleton = MakeChain();

    AnimationJointTrack track;
    track.JointIndex = 0;
    track.Path = AnimationChannelPath::Rotation;
    track.Interpolation = AnimationInterpolation::Linear;
    track.TimesSeconds = { 0.0f, 1.0f };
    const Quatf from = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    const Quatf to = Quatf::FromAxisAngle(Vec3d::Up(), 1.5f);
    for (const Quatf& value : { from, to })
    {
        track.Values.push_back(value.X);
        track.Values.push_back(value.Y);
        track.Values.push_back(value.Z);
        track.Values.push_back(value.W);
    }
    AnimationClipData clip;
    clip.DurationSeconds = 1.0f;
    clip.Tracks.push_back(track);

    std::vector<Transform3f> pose;
    SampleAnimationClip(clip, skeleton, 0.5f, pose);

    const Quatf expected = Quatf::Slerp(from, to, 0.5f);
    EXPECT_NEAR(pose[0].Rotation.W, expected.W, 1e-5f);
    EXPECT_NEAR(pose[0].Rotation.Y, expected.Y, 1e-5f);
    const float length = std::sqrt(
        pose[0].Rotation.X * pose[0].Rotation.X
        + pose[0].Rotation.Y * pose[0].Rotation.Y
        + pose[0].Rotation.Z * pose[0].Rotation.Z
        + pose[0].Rotation.W * pose[0].Rotation.W);
    EXPECT_NEAR(length, 1.0f, 1e-5f);
}

// The whole chain, and the invariant the pre-skin dispatch rests on: a pose
// that happens to equal the bind pose produces identity palette entries, so
// a skinned mesh posed at bind draws byte-exactly like rest geometry.
TEST(AnimationClipSampling, APoseEqualToBindYieldsTheIdentityPalette)
{
    const SkeletonData skeleton = MakeChain();

    // A clip whose only track restates the tip's bind translation.
    AnimationClipData clip;
    clip.DurationSeconds = 1.0f;
    clip.Tracks.push_back(MakeTranslationTrack(
        1, AnimationInterpolation::Linear, { 0.0f, 1.0f },
        { skeleton.Joints[1].BindTranslation,
          skeleton.Joints[1].BindTranslation }));

    std::vector<Transform3f> pose;
    SampleAnimationClip(clip, skeleton, 0.4f, pose);
    std::vector<Mat4> model;
    BuildPosedModelTransforms(skeleton, pose, model);
    std::vector<Mat4> palette;
    BuildSkinningPalette(skeleton, model, palette);

    ASSERT_EQ(palette.size(), 2u);
    const Mat4 identity = Mat4::Identity();
    for (const Mat4& entry : palette)
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_NEAR(entry[row][column], identity[row][column], 1e-5f);
}

// A posed joint moves its own model transform and its children's, and the
// palette entry stops being identity exactly where the pose left bind.
TEST(AnimationClipSampling, APosedJointCarriesItsChildren)
{
    const SkeletonData skeleton = MakeChain();

    AnimationClipData clip;
    clip.DurationSeconds = 1.0f;
    clip.Tracks.push_back(MakeTranslationTrack(
        0, AnimationInterpolation::Linear, { 0.0f, 1.0f },
        { skeleton.Joints[0].BindTranslation,
          skeleton.Joints[0].BindTranslation + Vec3d(0.0f, 10.0f, 0.0f) }));

    std::vector<Transform3f> pose;
    SampleAnimationClip(clip, skeleton, 1.0f, pose);
    std::vector<Mat4> model;
    BuildPosedModelTransforms(skeleton, pose, model);

    std::vector<Mat4> bindModel;
    BuildBindModelTransforms(skeleton, bindModel);
    // Both joints translated by the root's 10 units, the child inheriting it.
    EXPECT_NEAR(model[0][1][3], bindModel[0][1][3] + 10.0f, 1e-5f);
    EXPECT_NEAR(model[1][1][3], bindModel[1][1][3] + 10.0f, 1e-5f);
}
