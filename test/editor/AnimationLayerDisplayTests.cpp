// The viewport shows the pose pass's composed pose, or its layers recomposed
// without the muted or soloed-away ones. That display choice never reaches the
// rig or the pass's state.

#include "authoring/AnimationPreviewWorkspace.h"

#include <anim/AnimationClipCache.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    // Two joints; a base layer that moved joint 0 to x = 2 and an upper
    // override, masked to joint 1, that moved it to x = 5.
    struct DisplayFixture
    {
        SkeletonData Skeleton;
        AnimationClipCache Clips;
        AnimBoundRig Rig;
        AnimPoseState State;
        AnimPosePool Pool;
        AnimPosePool::Slot* Slot = nullptr;
        AnimPoseScratch Scratch;

        DisplayFixture()
        {
            for (int parent : { -1, 0 })
            {
                SkeletonJoint joint;
                joint.ParentIndex = parent;
                Skeleton.Joints.push_back(joint);
            }
            for (const char* name : { "anim.layer.base", "anim.layer.upper" })
            {
                AnimBoundLayer layer;
                layer.NameText = name;
                Rig.Layers.push_back(std::move(layer));
            }
            Rig.Layers[1].Mask = { 0, 1 };
            AnimBoundContent content;
            content.Path = "asset://anim/any.sanim";
            Rig.Contents.push_back(content);

            State.Slot = Pool.Allocate(EntityId{});
            Slot = Pool.Find(State.Slot, EntityId{});
            Slot->Shape(2, 2);
            Slot->LayerPose(0)[0].Position = Vec3d(2.0f, 0.0f, 0.0f);
            Slot->LayerPose(1)[1].Position = Vec3d(5.0f, 0.0f, 0.0f);
            Slot->Current[0].Position = Vec3d(2.0f, 0.0f, 0.0f);
            Slot->Current[1].Position = Vec3d(5.0f, 0.0f, 0.0f);
            Slot->HasCurrent = true;
            for (AnimLayerPose& layer : State.Layers)
                layer.Playing.Content = 0;
        }

        std::vector<Transform3f> Shown(const AnimationLayerDisplay& display)
        {
            std::vector<Transform3f> pose;
            AnimationPreviewDisplayPose(AnimPoseSources{ &Rig, &Clips, &Skeleton }, *Slot, State, nullptr, display, 0,
                                        1.0 / 60.0, Scratch, pose);
            return pose;
        }
    };
}

TEST(AnimationLayerDisplay, WithEveryLayerShownItIsThePosePassesOwnPose)
{
    DisplayFixture fx;
    // Mark the composed pose so it can be told apart from a recomposition.
    fx.Slot->Current[0].Position.Y = 7.0f;
    const std::vector<Transform3f> pose = fx.Shown({});
    ASSERT_EQ(pose.size(), 2u);
    EXPECT_FLOAT_EQ(pose[0].Position.Y, 7.0f);
}

TEST(AnimationLayerDisplay, MuteAndSoloRecomposeWithoutTouchingThePass)
{
    DisplayFixture fx;
    AnimationLayerDisplay muted;
    muted.Muted = 0b10;
    std::vector<Transform3f> pose = fx.Shown(muted);
    EXPECT_FLOAT_EQ(pose[0].Position.X, 2.0f);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 0.0f) << "the upper layer is muted: joint 1 at bind";

    AnimationLayerDisplay soloed;
    soloed.Soloed = 0b10;
    pose = fx.Shown(soloed);
    EXPECT_FLOAT_EQ(pose[0].Position.X, 0.0f) << "only the upper layer: joint 0 at bind";
    EXPECT_FLOAT_EQ(pose[1].Position.X, 5.0f);

    // Muting wins over a solo on the same layer.
    soloed.Muted = 0b10;
    pose = fx.Shown(soloed);
    EXPECT_FLOAT_EQ(pose[1].Position.X, 0.0f);

    // What the pass keeps is unchanged.
    EXPECT_FLOAT_EQ(fx.Slot->Current[1].Position.X, 5.0f);
    EXPECT_FLOAT_EQ(fx.Slot->LayerPose(1)[1].Position.X, 5.0f);
}
