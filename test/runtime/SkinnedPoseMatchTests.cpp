// A skinned mesh takes its rig's pose only when the rig poses the skeleton the
// mesh skins; otherwise it draws at bind, and extraction says so once per entity.

#include <render/extract/RenderExtractionSystem.h>

#include <gtest/gtest.h>

namespace
{
    SkeletonHandle Skeleton(std::uint32_t index)
    {
        SkeletonHandle handle;
        handle.Index = index;
        handle.Generation = 1;
        return handle;
    }

    AnimPosePool::Slot Posed(SkeletonHandle skeleton, std::uint32_t joints)
    {
        AnimPosePool::Slot slot;
        slot.Skeleton = skeleton;
        slot.Shape(joints, 1);
        slot.HasCurrent = true;
        return slot;
    }
}

TEST(SkinnedPoseMatch, AMeshTakesOnlyAPoseOfItsOwnSkeleton)
{
    const AnimPosePool::Slot pose = Posed(Skeleton(1), 3);
    EXPECT_EQ(MatchSkinnedPose(&pose, Skeleton(1), 3), SkinnedPoseMatch::Posed);
    EXPECT_EQ(MatchSkinnedPose(&pose, Skeleton(2), 3), SkinnedPoseMatch::OtherSkeleton);
    EXPECT_EQ(MatchSkinnedPose(&pose, Skeleton(1), 4), SkinnedPoseMatch::OtherSkeleton);
}

TEST(SkinnedPoseMatch, NothingPosedYetIsNotAMismatch)
{
    AnimPosePool::Slot pose = Posed(Skeleton(1), 3);
    pose.HasCurrent = false;
    EXPECT_EQ(MatchSkinnedPose(&pose, Skeleton(2), 3), SkinnedPoseMatch::Unposed);
    EXPECT_EQ(MatchSkinnedPose(nullptr, Skeleton(2), 3), SkinnedPoseMatch::Unposed);
}
