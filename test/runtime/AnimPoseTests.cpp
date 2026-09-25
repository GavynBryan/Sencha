// A layer absorbs a change in what it plays by its blend policy (snap, crossfade
// or inertialization from what was shown), and layers compose into the entity's
// pose identically for any worker count.

#include "AnimRigFixture.h"

#include <anim/AnimPoseSystem.h>
#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <cmath>
#include <format>
#include <memory>
#include <string>

TEST(AnimInertialization, AnOffsetDecaysFromWhatWasShownToRest)
{
    Transform3f shown;
    shown.Position = Vec3d(4.0f, 0.0f, 0.0f);
    shown.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.8f);
    const Transform3f target;
    const AnimJointOffset offset = AnimInertializeJoint(shown, shown, target, 0.2f, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(offset.Distance, 4.0f);
    EXPECT_NEAR(offset.Angle, 0.8f, 1e-5f);

    Transform3f pose = target;
    AnimApplyJointOffset(offset, 0.0f, pose);
    EXPECT_NEAR(pose.Position.X, 4.0f, 1e-5f) << "it starts from what was shown";
    EXPECT_TRUE(pose.Rotation.NearlyEquals(shown.Rotation, 1e-5f));

    // Still, it falls monotonically and arrives at rest when its time is up.
    float last = 4.0f;
    for (int step = 1; step <= 12; ++step)
    {
        Transform3f at = target;
        AnimApplyJointOffset(offset, static_cast<float>(step) * 0.2f / 12.0f, at);
        EXPECT_LE(at.Position.X, last + 1e-5f);
        last = at.Position.X;
    }
    EXPECT_NEAR(last, 0.0f, 1e-5f);
}

TEST(AnimInertialization, VelocityAwayIsDroppedAndVelocityTowardShortensTheDecay)
{
    const Transform3f target;
    Transform3f shown;
    shown.Position = Vec3d(1.0f, 0.0f, 0.0f);
    Transform3f before = shown;
    before.Position.X = 0.9f;
    // Moving away from the target: the decay does not start by overshooting.
    AnimJointOffset away = AnimInertializeJoint(shown, before, target, 0.5f, 0.1);
    EXPECT_FLOAT_EQ(away.Speed, 0.0f);
    EXPECT_FLOAT_EQ(away.Seconds, 0.5f);

    // Moving toward it fast: a decay long enough to overshoot is cut to
    // five times distance over speed; a shorter one keeps its time.
    before.Position.X = 1.5f;
    AnimJointOffset toward = AnimInertializeJoint(shown, before, target, 0.5f, 0.1);
    EXPECT_FLOAT_EQ(toward.Speed, -5.0f);
    EXPECT_FLOAT_EQ(toward.Seconds, 0.5f);
    toward = AnimInertializeJoint(shown, before, target, 2.0f, 0.1);
    EXPECT_FLOAT_EQ(toward.Seconds, 1.0f);
}

namespace
{
    // Two joints; idle holds both at x = 0, walk holds joint 0 at x = 4, and
    // wave holds joint 1 at z = 3. The upper layer is masked to joint 1.
    struct PoseFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;
        std::unique_ptr<JobSystem> Workers;
        std::unique_ptr<AnimPoseSystem> Poser;

        explicit PoseFixture(std::string_view walkBlend, std::uint32_t workers = 0, std::string_view overrides = {})
        {
            for (const char* tag : { "Anim.Idle", "Anim.Walk", "Anim.Wave", "Anim.Rest", "anim.intent.wave",
                                     "anim.layer.upper" })
                (void)Tags().RegisterTag(tag);
            SkeletonData skeleton;
            for (int parent : { -1, 0 })
            {
                SkeletonJoint joint;
                joint.Name = parent < 0 ? "root" : "arm";
                joint.ParentIndex = parent;
                skeleton.Joints.push_back(joint);
            }
            (void)Skeletons.Register("asset://anim/p.sskel", std::move(skeleton));
            HoldClip("asset://anim/idle.sanim", 0, Vec3d(0.0f, 0.0f, 0.0f));
            HoldClip("asset://anim/walk.sanim", 0, Vec3d(4.0f, 0.0f, 0.0f));
            HoldClip("asset://anim/wave.sanim", 1, Vec3d(0.0f, 0.0f, 3.0f));
            HoldClip("asset://anim/rest.sanim", 1, Vec3d(0.0f, 0.0f, 0.0f));

            (void)Load("asset://anim/p.facts.sdata", kAnimFactSchemaType,
                       R"({ "slots": [ { "name": "Speed", "kind": "float" } ] })");
            (void)Load("asset://anim/p.requests.sdata", kAnimRequestSchemaType,
                       R"({ "intents": [ { "intent": "anim.intent.wave", "params": [] } ] })");
            (void)Load("asset://anim/p.behaviors.sdata", kAnimBehaviorSetType,
                       std::format(R"({{ "behaviors": [
                           {{ "tag": "Anim.Idle", "kind": "cyclic", "blend": {} }},
                           {{ "tag": "Anim.Walk", "kind": "cyclic", "blend": {} }},
                           {{ "tag": "Anim.Rest", "kind": "cyclic" }},
                           {{ "tag": "anim.intent.wave", "kind": "cyclic", "blend": {{ "in": "snap" }} }} ] }})",
                                   walkBlend, walkBlend));
            (void)Load("asset://anim/p.selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" },
                { "name": "walk", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
                  "behavior": "Anim.Walk" } ] })");
            (void)Load("asset://anim/p.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" },
                { "behavior": "Anim.Rest", "clip": "asset://anim/rest.sanim" },
                { "behavior": "anim.intent.wave", "clip": "asset://anim/wave.sanim" } ] })");
            std::string blendOverrides;
            if (!overrides.empty())
            {
                (void)Load("asset://anim/p.overrides.sdata", kAnimBlendOverridesType, overrides);
                blendOverrides = R"("blend_overrides": [ "asset://anim/p.overrides.sdata" ],)";
            }
            Rig = Load("asset://anim/p.rig.sdata", kAnimRigType, std::format(R"({{
                "skeleton": "asset://anim/p.sskel",
                "facts": "asset://anim/p.facts.sdata", "requests": "asset://anim/p.requests.sdata", {}
                "behaviors": [ "asset://anim/p.behaviors.sdata" ], "slot_maps": [ "asset://anim/p.slots.sdata" ],
                "layers": [ {{ "name": "anim.layer.base", "selector": "asset://anim/p.selector.sdata",
                               "idle": "Anim.Idle" }},
                            {{ "name": "anim.layer.upper", "idle": "Anim.Rest", "mask": [ {{ "joint": "arm" }} ] }} ] }})",
                                                                               blendOverrides));
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            if (workers > 0)
                Workers = std::make_unique<JobSystem>(workers);
            Poser = std::make_unique<AnimPoseSystem>(Workers.get());
            Entity = Character(Rig);
        }

        // A one-second clip holding `joint` at `at`, every other joint at bind.
        void HoldClip(std::string_view path, std::uint32_t joint, Vec3d at)
        {
            AnimationClipData clip;
            clip.DurationSeconds = 1.0f;
            clip.SkeletonPath = "asset://anim/p.sskel";
            AnimationJointTrack track;
            track.JointIndex = joint;
            track.Path = AnimationChannelPath::Translation;
            track.TimesSeconds = { 0.0f };
            track.Values = { at.X, at.Y, at.Z };
            clip.Tracks.push_back(std::move(track));
            (void)Clips.Register(path, std::move(clip), Skeletons.AcquireOwned("asset://anim/p.sskel"));
        }

        void Step(int ticks = 1)
        {
            for (int i = 0; i < ticks; ++i)
            {
                Tick();
                Poser->Pose(Entities, Last(), kTick);
            }
        }

        const AnimPosePool::Slot& Pose(EntityId entity)
        {
            const World& reader = Entities;
            const AnimPoseState* state = reader.TryGet<AnimPoseState>(entity);
            EXPECT_NE(state, nullptr);
            const AnimPosePool::Slot* slot = Entities.GetResource<AnimPosePool>().Find(state->Slot);
            EXPECT_NE(slot, nullptr);
            return *slot;
        }
        float X(EntityId entity) { return Pose(entity).Current[0].Position.X; }
        float X() { return X(Entity); }
    };
}

TEST(AnimPose, ARiggedEntityIsPosedIntoItsSlotAndReleasesItOnDestroy)
{
    PoseFixture fx(R"({ "in": "snap" })");
    fx.Step();
    EXPECT_EQ(fx.Poser->Posed(), 1u);
    EXPECT_EQ(fx.Pose(fx.Entity).Joints, 2u);
    EXPECT_EQ(fx.Pose(fx.Entity).Layers, 2u);
    EXPECT_TRUE(fx.Pose(fx.Entity).HasCurrent);
    EXPECT_FALSE(fx.Pose(fx.Entity).HasPrevious);
    fx.Step();
    EXPECT_TRUE(fx.Pose(fx.Entity).HasPrevious);
    EXPECT_EQ(fx.Entities.GetResource<AnimPosePool>().LiveCount(), 1u);

    fx.Entities.DestroyEntity(fx.Entity);
    fx.Step();
    EXPECT_EQ(fx.Entities.GetResource<AnimPosePool>().LiveCount(), 0u);
}

TEST(AnimPose, ASnapTakesTheNewPoseAtOnce)
{
    PoseFixture fx(R"({ "in": "snap" })");
    fx.Step(5);
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Step();
    EXPECT_FLOAT_EQ(fx.X(), 4.0f);
}

// Inertialized, the change tick shows what was shown before, and the offset
// decays into the new pose over the blend.
TEST(AnimPose, AnInertializedChangeDecaysFromWhatWasShown)
{
    PoseFixture fx(R"({ "in": "inertialize", "in_ms": 100 })");
    fx.Step(5);
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Step();
    EXPECT_NEAR(fx.X(), 0.0f, 1e-5f);
    float last = 0.0f;
    for (int tick = 1; tick < 6; ++tick)
    {
        fx.Step();
        EXPECT_GT(fx.X(), last) << tick;
        EXPECT_LT(fx.X(), 4.0f) << tick;
        last = fx.X();
    }
    fx.Step();
    EXPECT_NEAR(fx.X(), 4.0f, 1e-5f) << "100 ms at 60 Hz is six ticks";

    const AnimDecisionRecord* blend = fx.LastRecord(fx.Entity, AnimDecisionCause::BlendApplied);
    ASSERT_NE(blend, nullptr);
    EXPECT_EQ(blend->Blend, AnimBlendMode::Inertialize);
    EXPECT_FLOAT_EQ(blend->BlendSeconds, 0.1f);
    EXPECT_NEAR(blend->BlendMagnitude, 4.0f, 1e-5f);
    EXPECT_EQ(blend->PreviousBehavior, fx.Tag("Anim.Idle"));
    EXPECT_EQ(blend->Behavior, fx.Tag("Anim.Walk"));
    EXPECT_FALSE(blend->BlendOverridden);
}

// A change while an offset decays starts from what is shown, offset and all:
// the pose never jumps.
TEST(AnimPose, StackedChangesFoldIntoOneOffset)
{
    PoseFixture fx(R"({ "in": "inertialize", "in_ms": 200 })");
    fx.Step(5);
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Step(4);
    const float shownBefore = fx.X();
    ASSERT_GT(shownBefore, 0.0f);
    ASSERT_LT(shownBefore, 4.0f);

    fx.Motion(fx.Entity).Speed = 0.0f;
    fx.Step();
    // Back toward idle from where the walk blend had got to, not from 4.
    EXPECT_GT(fx.X(), 0.0f);
    EXPECT_LT(std::abs(fx.X() - shownBefore), 0.5f);
    fx.Step(12);
    EXPECT_NEAR(fx.X(), 0.0f, 1e-5f);
}

// Crossfaded, both poses are alive: the incoming share rises over the blend.
TEST(AnimPose, ACrossfadeBlendsBothPlaybacks)
{
    PoseFixture fx(R"({ "in": "crossfade", "in_ms": 100 })");
    fx.Step(5);
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Step();
    EXPECT_NEAR(fx.X(), 0.0f, 1e-5f);
    fx.Step(3);
    // Half way through: smoothstep of 0.5 in, the same out, so an even share.
    EXPECT_NEAR(fx.X(), 2.0f, 1e-4f);
    fx.Step(3);
    EXPECT_NEAR(fx.X(), 4.0f, 1e-5f);
    const AnimDecisionRecord* blend = fx.LastRecord(fx.Entity, AnimDecisionCause::BlendApplied);
    ASSERT_NE(blend, nullptr);
    EXPECT_EQ(blend->Blend, AnimBlendMode::Crossfade);
}

TEST(AnimPose, APairwiseOverrideChoosesTheBlendAndSaysSo)
{
    PoseFixture fx(R"({ "in": "inertialize", "in_ms": 500 })", 0, R"({ "overrides": [
        { "from": "Anim.Idle", "to": "Anim.Walk", "blend": { "in": "snap" } } ] })");
    fx.Step(5);
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Step();
    EXPECT_FLOAT_EQ(fx.X(), 4.0f);
    const AnimDecisionRecord* blend = fx.LastRecord(fx.Entity, AnimDecisionCause::BlendApplied);
    ASSERT_NE(blend, nullptr);
    EXPECT_EQ(blend->Blend, AnimBlendMode::Snap);
    EXPECT_TRUE(blend->BlendOverridden);
}

// A change on the base layer blends inside the base layer: the upper layer's
// joint, which the upper layer covers, never moves.
TEST(AnimPose, EachLayerBlendsInsideItself)
{
    PoseFixture fx(R"({ "in": "inertialize", "in_ms": 100 })");
    ASSERT_TRUE(fx.Issue(fx.Entity, "anim.intent.wave").Accepted());
    fx.Step(5);
    EXPECT_FLOAT_EQ(fx.Pose(fx.Entity).Current[1].Position.Z, 3.0f);
    fx.Motion(fx.Entity).Speed = 1.0f;
    for (int tick = 0; tick < 8; ++tick)
    {
        fx.Step();
        EXPECT_FLOAT_EQ(fx.Pose(fx.Entity).Current[1].Position.Z, 3.0f) << tick;
        EXPECT_FLOAT_EQ(fx.Pose(fx.Entity).Current[1].Position.X, 0.0f) << tick;
    }
}

// The serial pass is the reference: any number of workers poses every entity
// to the same bits.
TEST(AnimPose, TheParallelPassMatchesTheSerialOne)
{
    constexpr std::string_view kBlend = R"({ "in": "inertialize", "in_ms": 150 })";
    const auto run = [&](std::uint32_t workers) {
        PoseFixture fx(kBlend, workers);
        std::vector<EntityId> crowd{ fx.Entity };
        for (int i = 0; i < 47; ++i)
            crowd.push_back(fx.Character(fx.Rig));
        std::vector<std::vector<Transform3f>> poses;
        for (int tick = 0; tick < 90; ++tick)
        {
            for (std::size_t i = 0; i < crowd.size(); ++i)
                fx.Motion(crowd[i]).Speed = ((tick + static_cast<int>(i) * 3) / 10) % 2 == 0 ? 0.0f : 1.0f;
            if (tick == 30)
                for (std::size_t i = 0; i < crowd.size(); i += 5)
                    (void)fx.Issue(crowd[i], "anim.intent.wave");
            fx.Step();
            for (const EntityId entity : crowd)
                poses.push_back(fx.Pose(entity).Current);
        }
        return poses;
    };
    const auto serial = run(0);
    const auto parallel = run(3);
    ASSERT_EQ(serial.size(), parallel.size());
    for (std::size_t i = 0; i < serial.size(); ++i)
        for (std::size_t j = 0; j < serial[i].size(); ++j)
            ASSERT_TRUE(serial[i][j] == parallel[i][j]) << "record " << i << " joint " << j;
}

// A rotation a rounding off unit length, with no vector part, is no rotation:
// it must not produce an axis of zero length.
TEST(AnimInertialization, ANearUnitRotationWithoutAnAxisIsAtRest)
{
    Transform3f shown;
    shown.Rotation = Quatf(0.0f, 0.0f, 0.0f, 0.9995f);
    const Transform3f target;
    const AnimJointOffset offset = AnimInertializeJoint(shown, shown, target, 0.2f, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(offset.Angle, 0.0f);
    Transform3f pose = target;
    AnimApplyJointOffset(offset, 0.0f, pose);
    EXPECT_TRUE(pose.Rotation.NearlyEquals(target.Rotation));
}
