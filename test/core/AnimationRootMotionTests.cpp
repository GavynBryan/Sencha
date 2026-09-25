// Root motion as data: a clip's root travel moves out of the pose and into a
// curve at cook, the curve survives the .sanim container, and the motion
// between two content times is read off it the same way every time.

#include <anim/AnimRootMotion.h>
#include <anim/Skeleton.h>
#include <assets/animation/AnimationClipSerializer.h>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AnimationRootExtraction.h>
#endif

namespace
{
    constexpr float kPi = std::numbers::pi_v<float>;

    // A one-second walk that travels 2 m forward (-Z), drifts 0.5 m right, and
    // turns a quarter left, over the root; the root is 1 m up and bobs.
    AnimationClipData TravellingClip(const Quat<float>& restTurn = {})
    {
        AnimationClipData clip;
        clip.SkeletonPath = "asset://test/walker.sskel";
        clip.DurationSeconds = 1.0f;
        AnimationJointTrack translation;
        translation.JointIndex = 0;
        translation.Path = AnimationChannelPath::Translation;
        translation.TimesSeconds = { 0.0f, 0.5f, 1.0f };
        translation.Values = { 0.0f, 1.0f, 0.0f, 0.25f, 1.1f, -1.0f, 0.5f, 1.0f, -2.0f };
        AnimationJointTrack rotation;
        rotation.JointIndex = 0;
        rotation.Path = AnimationChannelPath::Rotation;
        rotation.TimesSeconds = { 0.0f, 1.0f };
        for (const float yaw : { 0.0f, kPi / 2.0f })
        {
            const Quat<float> q = Quat<float>::FromAxisAngle(Vec3d{ 0.0f, 1.0f, 0.0f }, yaw) * restTurn;
            rotation.Values.insert(rotation.Values.end(), { q.X, q.Y, q.Z, q.W });
        }
        clip.Tracks = { translation, rotation };
        return clip;
    }

    SkeletonData Walker(const Quat<float>& restTurn = {})
    {
        SkeletonData skeleton;
        SkeletonJoint root;
        root.Name = "root";
        root.BindTranslation = Vec3d{ 0.0f, 1.0f, 0.0f };
        root.BindRotation = restTurn;
        SkeletonJoint spine;
        spine.Name = "spine";
        spine.ParentIndex = 0;
        skeleton.Joints = { root, spine };
        return skeleton;
    }

    AnimationRootCurve StraightLine()
    {
        // 3 m forward over 1 s, no turn.
        AnimationRootCurve curve;
        curve.TimesSeconds = { 0.0f, 1.0f };
        curve.Values = { 0.0f, 0.0f, 0.0f, 0.0f, -3.0f, 0.0f };
        return curve;
    }
}

TEST(AnimRootMotion, TheMotionBetweenTwoTimesIsReadInTheFrameTheRootFaced)
{
    AnimationRootCurve curve;
    curve.TimesSeconds = { 0.0f, 1.0f, 2.0f };
    // Forward 1 m, turn left a quarter, then forward 1 m in the new heading
    // (which in the clip's start frame is -X).
    curve.Values = { 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, kPi / 2.0f, -1.0f, -1.0f, kPi / 2.0f };

    const AnimRootDelta second = AnimRootMotionBetween(curve, 2.0f, 1.0, 2.0, false);
    EXPECT_NEAR(second.X, 0.0f, 1e-5f);
    EXPECT_NEAR(second.Z, -1.0f, 1e-5f) << "straight ahead, as the turned root sees it";
    EXPECT_NEAR(second.Yaw, 0.0f, 1e-5f);

    const AnimRootDelta whole = AnimRootMotionBetween(curve, 2.0f, 0.0, 2.0, false);
    EXPECT_NEAR(whole.X, -1.0f, 1e-5f);
    EXPECT_NEAR(whole.Z, -1.0f, 1e-5f);
    EXPECT_NEAR(whole.Yaw, kPi / 2.0f, 1e-5f);

    // One-shot content holds at its end.
    const AnimRootDelta past = AnimRootMotionBetween(curve, 2.0f, 2.0, 3.0, false);
    EXPECT_FLOAT_EQ(past.Z, 0.0f);
}

TEST(AnimRootMotion, CyclicContentCountsEveryLoopItCrosses)
{
    const AnimationRootCurve curve = StraightLine();
    const AnimRootDelta across = AnimRootMotionBetween(curve, 1.0f, 0.75, 2.25, true);
    EXPECT_NEAR(across.Z, -3.0f * 1.5f, 1e-4f);

    // Summing tick by tick is the same as asking once.
    float z = 0.0f;
    for (int tick = 0; tick < 150; ++tick)
        z += AnimRootMotionBetween(curve, 1.0f, tick / 60.0, (tick + 1) / 60.0, true).Z;
    EXPECT_NEAR(z, -3.0f * 2.5f, 1e-3f);
}

TEST(AnimRootMotion, ASanimKeepsItsRootCurve)
{
    AnimationClipData clip = TravellingClip();
    clip.Root = StraightLine();
    std::vector<std::byte> bytes;
    std::string error;
    ASSERT_TRUE(WriteSanimToBytes(clip, bytes, &error)) << error;
    AnimationClipData read;
    ASSERT_TRUE(LoadSanimFromBytes(bytes, read, &error)) << error;
    ASSERT_TRUE(read.Root.has_value());
    EXPECT_EQ(read.Root->TimesSeconds, clip.Root->TimesSeconds);
    EXPECT_EQ(read.Root->Values, clip.Root->Values);

    // And one without has none.
    clip.Root.reset();
    ASSERT_TRUE(WriteSanimToBytes(clip, bytes, &error)) << error;
    ASSERT_TRUE(LoadSanimFromBytes(bytes, read, &error)) << error;
    EXPECT_FALSE(read.Root.has_value());
}

TEST(AnimRootMotion, AMalformedRootCurveIsRefused)
{
    AnimationClipData clip = TravellingClip();
    clip.Root = StraightLine();
    clip.Root->TimesSeconds = { 0.0f, 2.0f };
    std::string error;
    EXPECT_FALSE(ValidateAnimationClipData(clip, &error));
    EXPECT_NE(error.find("root curve"), std::string::npos) << error;
}

#ifdef SENCHA_ENABLE_COOK

// The root's travel and turn leave the pose for the curve; its height and
// what it does not turn stay.
TEST(AnimRootMotionExtraction, TravelAndTurnMoveFromThePoseToTheCurve)
{
    AnimationClipData clip = TravellingClip();
    std::string error;
    ASSERT_TRUE(ExtractAnimationRootMotion(clip, Walker(), &error)) << error;
    ASSERT_TRUE(clip.Root.has_value());

    const AnimRootPose end = SampleAnimRootCurve(*clip.Root, 1.0f);
    EXPECT_NEAR(end.X, 0.5f, 1e-5f);
    EXPECT_NEAR(end.Z, -2.0f, 1e-5f);
    EXPECT_NEAR(end.Yaw, kPi / 2.0f, 1e-4f);
    const AnimRootPose middle = SampleAnimRootCurve(*clip.Root, 0.5f);
    EXPECT_NEAR(middle.Z, -1.0f, 1e-5f) << "keyed wherever the source was";

    const AnimationJointTrack& translation = clip.Tracks[0];
    for (std::size_t key = 0; key < translation.TimesSeconds.size(); ++key)
    {
        EXPECT_FLOAT_EQ(translation.Values[key * 3], 0.0f);
        EXPECT_FLOAT_EQ(translation.Values[key * 3 + 2], 0.0f);
    }
    EXPECT_FLOAT_EQ(translation.Values[4], 1.1f) << "the bob is the pose's";

    const AnimationJointTrack& rotation = clip.Tracks[1];
    for (std::size_t key = 0; key < rotation.TimesSeconds.size(); ++key)
    {
        const float* q = rotation.Values.data() + key * 4;
        EXPECT_NEAR(std::abs(q[3]), 1.0f, 1e-5f) << "no yaw left in key " << key;
    }
}

// A root whose rest pose is folded (an armature lying on its side, say) has
// no forward axis of its own that means facing; the turn is measured from the
// clip's first frame, so the fold does not read as yaw.
TEST(AnimRootMotionExtraction, AFoldedRestPoseIsNotMistakenForYaw)
{
    const Quat<float> fold = Quat<float>::FromAxisAngle(Vec3d{ 1.0f, 0.0f, 0.0f }, kPi / 2.0f);
    AnimationClipData clip = TravellingClip(fold);
    std::string error;
    ASSERT_TRUE(ExtractAnimationRootMotion(clip, Walker(fold), &error)) << error;
    EXPECT_NEAR(SampleAnimRootCurve(*clip.Root, 1.0f).Yaw, kPi / 2.0f, 1e-4f);
    const float* last = clip.Tracks[1].Values.data() + 4;
    EXPECT_NEAR(last[0], fold.X, 1e-4f) << "the fold stays in the pose";
    EXPECT_NEAR(last[3], fold.W, 1e-4f);
}

TEST(AnimRootMotionExtraction, TwoRootsCannotSayWhichCarriesTheCharacter)
{
    AnimationClipData clip = TravellingClip();
    SkeletonData skeleton = Walker();
    skeleton.Joints[1].ParentIndex = -1;
    std::string error;
    EXPECT_FALSE(ExtractAnimationRootMotion(clip, skeleton, &error));
    EXPECT_NE(error.find("2 root joints"), std::string::npos) << error;
}

#endif
