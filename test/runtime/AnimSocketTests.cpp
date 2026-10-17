#include <gtest/gtest.h>

#include "AnimRigFixture.h"

#include <math/Angle.h>

#include <format>
#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://anim/biped.sskel";
    constexpr const char* kRig = "asset://anim/s.rig.sdata";

    // root > pelvis > spine > neck > head; spine > arm_l > hand_l; a second "twin".
    SkeletonData Biped(bool twinned = false)
    {
        SkeletonData skeleton;
        const auto add = [&](const char* name, int parent) {
            SkeletonJoint joint;
            joint.Name = name;
            joint.ParentIndex = parent;
            skeleton.Joints.push_back(joint);
        };
        add("root", -1);
        add("pelvis", 0);
        add("spine", 1);
        add("neck", 2);
        add("head", 3);
        add("arm_l", 2);
        add("hand_l", 5);
        if (twinned)
        {
            add("twin", 0);
            add("twin", 0);
        }
        return skeleton;
    }

    void LoadRigContent(AnimRigFixture& fx)
    {
        (void)fx.Tags().RegisterTag("Anim.Idle");
        fx.Clip("asset://anim/idle.sanim", 1.0f, {}, kSkeleton);
        (void)fx.Load("asset://anim/s.behaviors.sdata", kAnimBehaviorSetType,
                      R"({ "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" } ] })");
        (void)fx.Load("asset://anim/s.slots.sdata", kAnimSlotMapType,
                      R"({ "rows": [ { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" } ] })");
    }

    std::string RigJson(std::string_view sockets, bool withSkeleton = true)
    {
        return std::format(R"({{ {}
            "behaviors": [ "asset://anim/s.behaviors.sdata" ], "slot_maps": [ "asset://anim/s.slots.sdata" ],
            "layers": [ {{ "name": "anim.layer.base", "idle": "Anim.Idle" }} ],
            "sockets": {} }})",
                           withSkeleton ? std::format(R"("skeleton": "{}",)", kSkeleton) : std::string(), sockets);
    }

    const AnimBoundRig& BindSockets(AnimRigFixture& fx, std::string_view sockets, bool withSkeleton = true)
    {
        LoadRigContent(fx);
        return fx.Bound(fx.Load(kRig, kAnimRigType, RigJson(sockets, withSkeleton)));
    }
}

TEST(AnimRigSockets, ANameFindsItsJointAndOffset)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    const AnimBoundRig& rig = BindSockets(fx, R"([
        { "name": "hand_l", "joint": "hand_l", "position": [0.1, 0, 0.2], "rotation": [0, 90, 0] },
        { "name": "head", "joint": "head" } ])");
    ASSERT_TRUE(rig.Valid) << AnimRigFixture::Describe(rig);
    EXPECT_TRUE(rig.Diagnostics.empty()) << AnimRigFixture::Describe(rig);

    const RigSocketIndex hand = rig.FindSocket("hand_l");
    ASSERT_TRUE(hand.IsValid());
    const AnimBoundSocket& bound = rig.Sockets[hand.Value];
    EXPECT_EQ(bound.Joint, 6u);
    const Transform3f expected(Vec3d(0.1f, 0.0f, 0.2f), Quat<float>::FromPitchYawRoll(0.0f, ToRadians(90.0f), 0.0f),
                               Vec3d::One());
    EXPECT_TRUE(bound.Offset.NearlyEquals(expected));

    const RigSocketIndex head = rig.FindSocket("head");
    ASSERT_TRUE(head.IsValid());
    EXPECT_EQ(rig.Sockets[head.Value].Joint, 4u);
    EXPECT_EQ(rig.Sockets[head.Value].Offset, Transform3f{}) << "no offset authored";

    const std::string text = "head_r";
    EXPECT_EQ(rig.FindSocket(std::string_view(text).substr(0, 4)), head) << "a view, not a terminated string";
    EXPECT_FALSE(rig.FindSocket("hand_r").IsValid());
    EXPECT_FALSE(rig.FindSocket("").IsValid());
}

TEST(AnimRigSockets, AnUnknownJointLeavesOnlyThatSocketUnbound)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    const AnimBoundRig& rig = BindSockets(fx, R"([
        { "name": "hand_r", "joint": "hand_r" }, { "name": "head", "joint": "head" } ])");
    EXPECT_TRUE(rig.Valid) << "a socket nothing can attach to does not stop the rig playing";
    const AnimDiagnostic* unknown = AnimRigFixture::FindCode(rig, "anim.socket.joint_unknown");
    ASSERT_NE(unknown, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(unknown->FieldPath, "$.data.sockets[0].joint");
    EXPECT_FALSE(rig.FindSocket("hand_r").IsValid());
    EXPECT_TRUE(rig.FindSocket("head").IsValid());
}

TEST(AnimRigSockets, AJointTwoBonesShareCannotBeNamed)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped(true));
    const AnimBoundRig& rig = BindSockets(fx, R"([ { "name": "twin", "joint": "twin" } ])");
    ASSERT_NE(AnimRigFixture::FindCode(rig, "anim.socket.joint_ambiguous"), nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_FALSE(rig.FindSocket("twin").IsValid());
}

TEST(AnimRigSockets, ASocketNeedsTheRigToNameItsSkeleton)
{
    AnimRigFixture fx;
    const AnimBoundRig& rig = BindSockets(fx, R"([ { "name": "head", "joint": "head" } ])", false);
    ASSERT_NE(AnimRigFixture::FindCode(rig, "anim.socket.no_skeleton"), nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_FALSE(rig.FindSocket("head").IsValid());
}

TEST(AnimRigSockets, AnUnloadedSkeletonIsAnErrorWhenSocketsNameItsJoints)
{
    AnimRigFixture fx;
    const AnimBoundRig& rig = BindSockets(fx, R"([ { "name": "head", "joint": "head" } ])");
    const AnimDiagnostic* unavailable = AnimRigFixture::FindCode(rig, "anim.rig.skeleton_unavailable");
    ASSERT_NE(unavailable, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(unavailable->Severity, AnimDiagnosticSeverity::Error);
}

TEST(AnimRigSockets, NamesAreUniqueShortAndNameAJoint)
{
    AnimRigFixture fx;
    EXPECT_NE(fx.CompileError(kAnimRigType, RigJson(R"([
        { "name": "head", "joint": "head" }, { "name": "head", "joint": "neck" } ])")).find("sockets[1].name"),
        std::string::npos);
    const std::string longName(kAnimSocketNameMaxLength + 1, 's');
    EXPECT_NE(fx.CompileError(kAnimRigType, RigJson(std::format(R"([ {{ "name": "{}", "joint": "head" }} ])", longName)))
                  .find("sockets[0].name"),
              std::string::npos);
    EXPECT_TRUE(fx.CompileError(kAnimRigType, RigJson(std::format(R"([ {{ "name": "{}", "joint": "head" }} ])",
                                                                  longName.substr(1))))
                    .empty());
    EXPECT_NE(fx.CompileError(kAnimRigType, RigJson(R"([ { "name": "head", "joint": "" } ])")).find("sockets[0].joint"),
              std::string::npos);
}

TEST(AnimRigSockets, AReloadedRigRebindsItsSockets)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    LoadRigContent(fx);
    const DataAssetHandle handle = fx.Load(kRig, kAnimRigType, RigJson(R"([ { "name": "grip", "joint": "hand_l" } ])"));
    const std::uint64_t before = fx.Bound(handle).Generation;
    ASSERT_EQ(fx.Bound(handle).Sockets[fx.Bound(handle).FindSocket("grip").Value].Joint, 6u);

    fx.Reload(kRig, kAnimRigType, RigJson(R"([ { "name": "grip", "joint": "head" }, { "name": "back", "joint": "spine" } ])"));
    const AnimBoundRig& moved = fx.Bound(handle);
    EXPECT_NE(moved.Generation, before) << "a holder of a socket index can tell it is stale";
    EXPECT_EQ(moved.Sockets[moved.FindSocket("grip").Value].Joint, 4u);
    EXPECT_TRUE(moved.FindSocket("back").IsValid());

    fx.Reload(kRig, kAnimRigType, RigJson("[]"));
    EXPECT_FALSE(fx.Bound(handle).FindSocket("grip").IsValid());
    EXPECT_TRUE(fx.Bound(handle).Sockets.empty());
}

TEST(AnimRigSockets, BindingMakesNoEntityForAJointOrASocket)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    const std::size_t before = fx.Entities.GetAliveEntities().size();
    const AnimBoundRig& rig = BindSockets(fx, R"([ { "name": "hand_l", "joint": "hand_l" }, { "name": "head", "joint": "head" } ])");
    ASSERT_EQ(rig.Sockets.size(), 2u);
    EXPECT_EQ(fx.Entities.GetAliveEntities().size(), before);
}
