#include <gtest/gtest.h>

#include "AllocationCounter.h"

#include <anim/AnimPosePool.h>
#include <anim/AnimationClipSampling.h>

#include <vector>

namespace
{
    // root > child > tip, each a unit along X from its parent at bind.
    SkeletonData Chain()
    {
        SkeletonData skeleton;
        for (int j = 0; j < 3; ++j)
        {
            SkeletonJoint joint;
            joint.Name = "j" + std::to_string(j);
            joint.ParentIndex = j - 1;
            skeleton.Joints.push_back(joint);
        }
        return skeleton;
    }

    AnimPosePool::Slot Posed(bool withPrevious)
    {
        AnimPosePool::Slot slot;
        slot.Shape(3, 1);
        for (std::size_t j = 0; j < 3; ++j)
        {
            slot.Previous[j].Position = Vec3d(1.0f, 0.0f, 0.0f);
            slot.Current[j].Position = Vec3d(1.0f, 2.0f, 0.0f);
            slot.Current[j].Rotation = Quat<float>::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), 0.5f);
        }
        slot.HasCurrent = true;
        slot.HasPrevious = withPrevious;
        ++slot.Revision;
        return slot;
    }

    std::vector<Mat4> Expected(const AnimPosePool::Slot& slot, const SkeletonData& skeleton, float alpha)
    {
        std::vector<Transform3f> local(slot.Current.size());
        for (std::size_t j = 0; j < local.size(); ++j)
            local[j] = slot.HasPrevious && alpha < 1.0f ? Transform3f::Interpolate(slot.Previous[j], slot.Current[j], alpha)
                                                        : slot.Current[j];
        std::vector<Mat4> model;
        BuildPosedModelTransforms(skeleton, local, model);
        return model;
    }

    void ExpectSame(std::span<const Mat4> actual, const std::vector<Mat4>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t j = 0; j < expected.size(); ++j)
            EXPECT_EQ(actual[j], expected[j]) << "joint " << j;
    }
}

TEST(AnimPresentedPose, IsTheModelPoseBetweenTheLastTwoTicks)
{
    const SkeletonData skeleton = Chain();
    const AnimPosePool::Slot slot = Posed(true);
    for (const float alpha : { 0.0f, 0.3f, 0.5f, 1.0f })
        ExpectSame(slot.PresentedModel(skeleton, alpha), Expected(slot, skeleton, alpha));
}

TEST(AnimPresentedPose, AFirstPoseIsShownWholeAtAnyBlend)
{
    const SkeletonData skeleton = Chain();
    const AnimPosePool::Slot slot = Posed(false);
    ExpectSame(slot.PresentedModel(skeleton, 0.25f), Expected(slot, skeleton, 1.0f));
}

TEST(AnimPresentedPose, IsBuiltOncePerPoseAndBlend)
{
    const SkeletonData skeleton = Chain();
    AnimPosePool::Slot slot = Posed(true);
    const std::vector<Mat4> first(slot.PresentedModel(skeleton, 0.5f).begin(), slot.PresentedModel(skeleton, 0.5f).end());

    // Written behind the revision's back: a cached answer cannot see it.
    slot.Current[0].Position = Vec3d(9.0f, 9.0f, 9.0f);
    ExpectSame(slot.PresentedModel(skeleton, 0.5f), first);

    ++slot.Revision;
    ExpectSame(slot.PresentedModel(skeleton, 0.5f), Expected(slot, skeleton, 0.5f));
    // Another blend is another pose.
    ExpectSame(slot.PresentedModel(skeleton, 0.75f), Expected(slot, skeleton, 0.75f));
}

TEST(AnimPresentedPose, ReshapingMovesTheRevision)
{
    AnimPosePool::Slot slot = Posed(true);
    const std::uint64_t before = slot.Revision;
    slot.Shape(3, 1);
    EXPECT_EQ(slot.Revision, before) << "the same shape keeps the pose";
    slot.Shape(4, 1);
    EXPECT_NE(slot.Revision, before);
}

TEST(AnimPresentedPose, ASteadyPoseAllocatesNothing)
{
    const SkeletonData skeleton = Chain();
    AnimPosePool::Slot slot = Posed(true);
    (void)slot.PresentedModel(skeleton, 0.5f);
    const std::size_t before = AllocationCount();
    for (int frame = 0; frame < 8; ++frame)
    {
        ++slot.Revision;
        (void)slot.PresentedModel(skeleton, 0.125f * static_cast<float>(frame));
    }
    EXPECT_EQ(AllocationCount() - before, 0u);
}
