// Composing layers into one local pose: bind first, then each layer over the
// joints its mask covers, overriding toward its pose or adding what its pose
// changes from its reference, at its weight.

#include <gtest/gtest.h>

#include <anim/AnimPoseComposition.h>
#include <anim/AnimationClipSampling.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{
    // root > spine > arm, all at the origin, unrotated.
    SkeletonData Chain()
    {
        SkeletonData skeleton;
        for (int parent : { -1, 0, 1 })
        {
            SkeletonJoint joint;
            joint.ParentIndex = parent;
            joint.BindTranslation = Vec3d(0.0f, 1.0f, 0.0f);
            skeleton.Joints.push_back(joint);
        }
        return skeleton;
    }

    AnimationJointTrack Track(std::uint32_t joint, AnimationChannelPath path, std::vector<float> times,
                              std::vector<float> values)
    {
        AnimationJointTrack track;
        track.JointIndex = joint;
        track.Path = path;
        track.TimesSeconds = std::move(times);
        track.Values = std::move(values);
        return track;
    }

    // Every joint moves from x = a to x = b over one second.
    AnimationClipData Slide(float a, float b)
    {
        AnimationClipData clip;
        clip.DurationSeconds = 1.0f;
        for (std::uint32_t j = 0; j < 3; ++j)
            clip.Tracks.push_back(Track(j, AnimationChannelPath::Translation, { 0.0f, 1.0f },
                                        { a, 1.0f, 0.0f, b, 1.0f, 0.0f }));
        return clip;
    }

    // Every joint turns from 0 to 90 degrees about Y over one second.
    AnimationClipData Turn()
    {
        AnimationClipData clip;
        clip.DurationSeconds = 1.0f;
        const Quatf end = Quatf::FromAxisAngle(Vec3d::Up(), 1.5707963f);
        for (std::uint32_t j = 0; j < 3; ++j)
            clip.Tracks.push_back(Track(j, AnimationChannelPath::Rotation, { 0.0f, 1.0f },
                                        { 0.0f, 0.0f, 0.0f, 1.0f, end.X, end.Y, end.Z, end.W }));
        return clip;
    }

    std::vector<Transform3f> Sample(const AnimationClipData& clip, const SkeletonData& skeleton, float time)
    {
        std::vector<Transform3f> pose;
        SampleAnimationClip(clip, skeleton, time, pose);
        return pose;
    }

    float YawOf(const Quatf& rotation)
    {
        const Vec3d forward = rotation.RotateVector(Vec3d(1.0f, 0.0f, 0.0f));
        return std::atan2(-forward.Z, forward.X);
    }
}

TEST(AnimPoseComposition, NoLayersIsTheBindPose)
{
    const SkeletonData skeleton = Chain();
    std::vector<Transform3f> pose;
    ComposeAnimPose(skeleton, {}, pose);
    ASSERT_EQ(pose.size(), 3u);
    for (const Transform3f& joint : pose)
        EXPECT_FLOAT_EQ(joint.Position.Y, 1.0f);
}

// An override replaces the covered joints at full weight, blends at partial
// weight, and leaves joints outside its mask as the layers below left them.
TEST(AnimPoseComposition, AnOverrideAppliesOverItsMaskAtItsWeight)
{
    const SkeletonData skeleton = Chain();
    const AnimationClipData base = Slide(2.0f, 2.0f);
    const AnimationClipData upper = Slide(10.0f, 10.0f);
    const std::array<std::uint8_t, 3> mask{ 0, 1, 1 };

    const std::vector<Transform3f> basePose = Sample(base, skeleton, 0.5f);
    const std::vector<Transform3f> upperPose = Sample(upper, skeleton, 0.5f);
    std::vector<Transform3f> pose;
    const std::array layers{ AnimPoseLayer{ basePose, {}, 1.0f, AnimLayerMode::Override, {} },
                             AnimPoseLayer{ upperPose, {}, 0.25f, AnimLayerMode::Override, mask } };
    ComposeAnimPose(skeleton, layers, pose);
    EXPECT_FLOAT_EQ(pose[0].Position.X, 2.0f) << "outside the mask: the base";
    EXPECT_FLOAT_EQ(pose[1].Position.X, 4.0f) << "a quarter of the way from 2 to 10";
    EXPECT_FLOAT_EQ(pose[2].Position.X, 4.0f);

    const std::array full{ layers[0], AnimPoseLayer{ upperPose, {}, 1.0f, AnimLayerMode::Override, mask } };
    ComposeAnimPose(skeleton, full, pose);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 10.0f);

    const std::array silent{ layers[0], AnimPoseLayer{ upperPose, {}, 0.0f, AnimLayerMode::Override, mask } };
    ComposeAnimPose(skeleton, silent, pose);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 2.0f) << "weight zero shows nothing";
}

// An additive layer adds its change from its own first frame: at that frame
// it adds nothing, and later it adds the difference on top of the base.
TEST(AnimPoseComposition, AnAdditiveAddsItsChangeFromItsFirstFrame)
{
    const SkeletonData skeleton = Chain();
    const AnimationClipData base = Slide(2.0f, 2.0f);
    const AnimationClipData breathe = Slide(5.0f, 7.0f);
    const AnimationClipData turn = Turn();

    const std::vector<Transform3f> basePose = Sample(base, skeleton, 0.0f);
    const std::vector<Transform3f> first = Sample(breathe, skeleton, 0.0f);
    const std::vector<Transform3f> last = Sample(breathe, skeleton, 1.0f);
    std::vector<Transform3f> pose;
    std::array layers{ AnimPoseLayer{ basePose, {}, 1.0f, AnimLayerMode::Override, {} },
                       AnimPoseLayer{ first, first, 1.0f, AnimLayerMode::Additive, {} } };
    ComposeAnimPose(skeleton, layers, pose);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 2.0f) << "at its reference an additive adds nothing";

    layers[1].Pose = last;
    ComposeAnimPose(skeleton, layers, pose);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 4.0f) << "2 from the base plus the 2 it moved";

    layers[1].Weight = 0.5f;
    ComposeAnimPose(skeleton, layers, pose);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 3.0f);

    // Rotation composes onto what is there: a turn added at half weight to a
    // base already turned by the same clip's end.
    const std::vector<Transform3f> turnStart = Sample(turn, skeleton, 0.0f);
    const std::vector<Transform3f> turnEnd = Sample(turn, skeleton, 1.0f);
    const std::array turned{ AnimPoseLayer{ turnEnd, {}, 1.0f, AnimLayerMode::Override, {} },
                             AnimPoseLayer{ turnEnd, turnStart, 0.5f, AnimLayerMode::Additive, {} } };
    ComposeAnimPose(skeleton, turned, pose);
    EXPECT_NEAR(YawOf(pose[2].Rotation), 1.5707963f * 1.5f, 1e-4f);
}
