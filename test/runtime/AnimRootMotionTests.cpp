// A request-driven clip carries a character through the movement pipeline and
// mover, which a wall can stop. A machine that never poses is carried the same,
// and a replayed tick is carried as the authority carried it.

#include "AnimRigFixture.h"

#include <abilities/AbilityKit.h>
#include <anim/AnimPoseSystem.h>
#include <anim/AnimRootMotionSource.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <movement/CharacterTickStep.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/RootMotionSource.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    const Vec3d kGravity{ 0.0f, -9.81f, 0.0f };
    const Vec3d kUp{ 0.0f, 1.0f, 0.0f };
    constexpr const char* kSkeleton = "asset://anim/runner.sskel";

    const StoragePartitionSet& Partitions()
    {
        static const StoragePartitionSet partitions = [] {
            StoragePartitionSet value;
            value.Add(StoragePartitionId::Default());
            return value;
        }();
        return partitions;
    }

    // A character on a floor, whose dash request plays a one-second clip that
    // carries it 3 m forward (-Z) while its root joint stays where it stood.
    // With a wall, the wall's face is 1.5 m ahead.
    struct RootMotionFixture : AnimRigFixture
    {
        PhysicsWorld Physics;
        CharacterMoverPool Movers{ Physics };
        FreeLocomotionSystem Locomotion{ kGravity, kUp };
        JumpExecutionSystem Jump;
        RootMotionSystem Root;
        MotionCompositionSystem Composition;
        std::unique_ptr<AnimPoseSystem> Poser;
        DataAssetHandle Rig;
        EntityId Character;

        // 3 m forward over a second, unless the test says otherwise.
        static AnimationRootCurve Straight()
        {
            return AnimationRootCurve{ .TimesSeconds = { 0.0f, 1.0f },
                                       .Values = { 0.0f, 0.0f, 0.0f, 0.0f, -3.0f, 0.0f } };
        }

        explicit RootMotionFixture(bool wall, bool presentsPose = false, AnimationRootCurve dash = Straight())
            : AnimRigFixture({ "Anim.Idle", "anim.intent.dash" })
        {
            ComponentRegistrar registrar(Entities);
            RegisterEngineComponents(registrar);
            RegisterMovement(Entities);
            if (presentsPose)
                Poser = std::make_unique<AnimPoseSystem>(nullptr);

            BodyDesc floor;
            floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
            floor.Motion = BodyMotion::Static;
            floor.Layer = CollisionLayer::Static;
            (void)Physics.AddBody(floor);
            if (wall)
            {
                BodyDesc face;
                face.Shape = CollisionShape::MakeBox(Vec3d(12.0f, 4.0f, 0.5f));
                face.Motion = BodyMotion::Static;
                face.Layer = CollisionLayer::Static;
                face.Position = Vec3d(0.0f, 2.0f, -2.0f);
                (void)Physics.AddBody(face);
            }

            SkeletonData skeleton;
            SkeletonJoint root;
            root.Name = "root";
            root.BindTranslation = Vec3d{ 0.0f, 1.0f, 0.0f };
            skeleton.Joints.push_back(root);
            (void)Skeletons.Register(kSkeleton, std::move(skeleton));
            for (const auto& [path, carries] : { std::pair{ "asset://anim/idle.sanim", false },
                                                 std::pair{ "asset://anim/dash.sanim", true } })
            {
                AnimationClipData clip;
                clip.DurationSeconds = 1.0f;
                clip.SkeletonPath = kSkeleton;
                // What the cook leaves the pose: the root where it stood.
                AnimationJointTrack stand;
                stand.Path = AnimationChannelPath::Translation;
                stand.TimesSeconds = { 0.0f };
                stand.Values = { 0.0f, 1.0f, 0.0f };
                clip.Tracks.push_back(stand);
                if (carries)
                    clip.Root = dash;
                (void)Clips.Register(path, std::move(clip), Skeletons.AcquireOwned(kSkeleton));
            }
            (void)Load("asset://anim/c.requests.sdata", kAnimRequestSchemaType,
                       R"({ "intents": [ { "intent": "anim.intent.dash", "params": [] } ] })");
            (void)Load("asset://anim/c.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Idle", "kind": "cyclic" },
                { "tag": "anim.intent.dash", "kind": "one_shot", "root_motion": true } ] })");
            (void)Load("asset://anim/c.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "behavior": "anim.intent.dash", "clip": "asset://anim/dash.sanim" } ] })");
            Rig = Load("asset://anim/c.rig.sdata", kAnimRigType, R"({
                "skeleton": "asset://anim/runner.sskel", "requests": "asset://anim/c.requests.sdata",
                "behaviors": [ "asset://anim/c.behaviors.sdata" ], "slot_maps": [ "asset://anim/c.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })");
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));

            Character = Entities.CreateEntity();
            Entities.AddComponent<LocalTransform>(
                Character, LocalTransform{ Transform3f{ Vec3d(0.0f, 1.5f, 0.0f), Quatf::Identity(), Vec3d::One() } });
            Entities.AddComponent<CharacterController>(Character, CharacterController{});
            Entities.AddComponent<CharacterMovement>(
                Character, CharacterMovement{ .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode() });
            Entities.AddComponent(Character, AnimRig{ Rig });
            Entities.AddComponent(Character, AnimDecisionLog{});
            Movers.Reconcile(Entities, Partitions());
        }

        // Animation decides what plays, then movement moves -- as the
        // schedule orders them.
        void Step()
        {
            const AnimTick tick = Now;
            Tick();
            Locomotion.Step(Entities, static_cast<float>(kTick));
            Jump.Step(Entities, static_cast<float>(kTick));
            Root.Step(Entities, tick, kTick);
            Composition.Step(Entities);
            Movers.Reconcile(Entities, Partitions());
            Movers.Drive(Entities, Partitions(), static_cast<float>(kTick), kGravity);
            if (Poser != nullptr)
                Poser->Pose(Entities, tick, kTick);
        }

        void StepTo(AnimTick tick)
        {
            while (Now <= tick)
                Step();
        }

        AnimRequestResult Dash(AnimRequestLifetime lifetime = AnimRequestLifetime::Held)
        {
            AnimRequestDesc desc;
            desc.Source = Character;
            desc.Intent = Tag("anim.intent.dash");
            desc.Lifetime = lifetime;
            return IssueAnimRequest(Entities, Character, desc, Now);
        }

        [[nodiscard]] Vec3d Position() const
        {
            const World& reader = Entities;
            return reader.TryGet<LocalTransform>(Character)->Value.Position;
        }

        // What a replay restores and then steps from.
        struct Saved
        {
            LocalTransform Transform;
            KinematicState Kinematics;
            SupportState Support;
        };
        [[nodiscard]] Saved Save() const
        {
            const World& reader = Entities;
            return { *reader.TryGet<LocalTransform>(Character), *reader.TryGet<KinematicState>(Character),
                     *reader.TryGet<SupportState>(Character) };
        }
        void Restore(const Saved& saved)
        {
            *Entities.TryGet<LocalTransform>(Character) = saved.Transform;
            *Entities.TryGet<KinematicState>(Character) = saved.Kinematics;
            *Entities.TryGet<SupportState>(Character) = saved.Support;
            ASSERT_TRUE(Movers.RestorePosition(Entities, Character, saved.Transform.Value.Position));
        }
        // Ticks `first`..`last` again through the replay kernel.
        void Replay(AnimTick first, AnimTick last)
        {
            for (AnimTick tick = first; tick <= last; ++tick)
                StepCharacterTick(Entities, &Movers, Character, MovementIntent{}, tick, static_cast<float>(kTick),
                                  kGravity, kUp);
        }
    };
}

// Carried 3 m when nothing is in the way, and stopped by a wall when
// something is: root motion goes through the mover, not around it.
TEST(AnimRootMotion, ARequestDrivenMoveCollidesAgainstAWall)
{
    RootMotionFixture open(false);
    RootMotionFixture walled(true);
    for (RootMotionFixture* carried : { &open, &walled })
    {
        carried->StepTo(30);
        ASSERT_TRUE(carried->Dash().Accepted());
        carried->StepTo(120);
    }
    EXPECT_NEAR(open.Position().Z, -3.0f, 0.05f) << "carried as far as the curve goes, once";
    EXPECT_NEAR(open.Position().X, 0.0f, 1e-3f);
    EXPECT_GT(walled.Position().Z, -1.5f) << "not through the wall's face";
    EXPECT_LT(walled.Position().Z, -1.0f) << "and up against it";
}

// A clip that turns a quarter to the left in place and then walks a metre
// ends facing -X a metre along -X: the turn reaches the character, and the
// walk after it is read in the turned frame.
TEST(AnimRootMotion, ATurnTurnsTheCharacterAndWhatFollowsGoesTheNewWay)
{
    constexpr float quarter = 1.5707964f;
    RootMotionFixture carried(false, false,
                              AnimationRootCurve{ .TimesSeconds = { 0.0f, 0.5f, 1.0f },
                                                  .Values = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, quarter, -1.0f, 0.0f, quarter } });
    carried.StepTo(30);
    ASSERT_TRUE(carried.Dash().Accepted());
    carried.StepTo(120);
    EXPECT_NEAR(carried.Position().X, -1.0f, 0.02f);
    EXPECT_NEAR(carried.Position().Z, 0.0f, 0.02f);
    const Vec3d forward = static_cast<const World&>(carried.Entities)
                              .TryGet<LocalTransform>(carried.Character)
                              ->Value.Rotation.RotateVector(Vec3d{ 0.0f, 0.0f, -1.0f });
    EXPECT_NEAR(forward.X, -1.0f, 1e-3f) << "facing -X";
}

// Asking what carries the character moves nothing; only a tick through the
// mover does.
TEST(AnimRootMotion, SamplingNeverMovesACapsule)
{
    RootMotionFixture carried(false);
    carried.StepTo(30);
    ASSERT_TRUE(carried.Dash().Accepted());
    carried.StepTo(40);
    const Vec3d before = carried.Position();
    RootMotionSample sample;
    for (int i = 0; i < 10; ++i)
        ASSERT_TRUE(SampleAnimRootMotion(carried.Entities, carried.Character, 45, AnimRigFixture::kTick, sample));
    EXPECT_LT(sample.PlanarVelocity.Z, -1.0f) << "it is being carried";
    EXPECT_EQ(carried.Position(), before);
}

// The travel the pose gave up is the capsule's alone: the posed root stays
// where it stood while the character crosses 3 m, so what is drawn moves
// once, not twice.
TEST(AnimRootMotion, AStrippedPoseDoesNotDoubleApply)
{
    RootMotionFixture carried(false, true);
    carried.StepTo(30);
    ASSERT_TRUE(carried.Dash().Accepted());
    for (AnimTick tick : { 45u, 60u, 90u })
    {
        carried.StepTo(tick);
        const AnimPosePool::Slot* pose = carried.Entities.GetResource<AnimPosePool>().Find(
            carried.Entities.TryGet<AnimPoseState>(carried.Character)->Slot);
        ASSERT_NE(pose, nullptr);
        ASSERT_TRUE(pose->HasCurrent);
        EXPECT_NEAR(pose->Current[0].Position.Z, 0.0f, 1e-5f) << "tick " << tick;
    }
    EXPECT_NEAR(carried.Position().Z, -3.0f, 0.05f);
}

// A machine that never poses -- an authority at the timing tier -- is
// carried tick for tick as one that does.
TEST(AnimRootMotion, TimingAndFullAgreeOnDisplacement)
{
    RootMotionFixture timing(true, false);
    RootMotionFixture full(true, true);
    for (RootMotionFixture* carried : { &timing, &full })
    {
        carried->StepTo(30);
        ASSERT_TRUE(carried->Dash().Accepted());
    }
    for (AnimTick tick = 31; tick <= 120; ++tick)
    {
        timing.StepTo(tick);
        full.StepTo(tick);
        ASSERT_EQ(timing.Position(), full.Position()) << "tick " << tick;
    }
}

// Replaying ticks after the authority cancelled the move carries the
// character as the authority did: up to the cancel and no further.
TEST(AnimRootMotion, AReplayIsCarriedThroughACancel)
{
    // The authority's run: dash on 30, cancelled on 50.
    RootMotionFixture authority(false);
    authority.StepTo(29);
    const AnimRequestResult dash = authority.Dash();
    ASSERT_TRUE(dash.Accepted());
    authority.StepTo(49);
    ASSERT_TRUE(CancelAnimRequest(authority.Entities, authority.Character, dash.Id, AnimCancelReason::Released,
                                  authority.Now));
    authority.StepTo(80);

    // A client that did not hear of the cancel in time: carried to 80.
    RootMotionFixture client(false);
    client.StepTo(29);
    ASSERT_TRUE(client.Dash().Accepted());
    client.StepTo(39);
    const RootMotionFixture::Saved at39 = client.Save();
    client.StepTo(80);
    ASSERT_LT(client.Position().Z, authority.Position().Z - 0.5f) << "the client went further";

    // The correction: the authority's request set, and a replay from 40.
    *client.Entities.TryGet<AnimRequestSet>(client.Character) =
        *static_cast<const World&>(authority.Entities).TryGet<AnimRequestSet>(authority.Character);
    client.Restore(at39);
    client.Replay(40, 80);
    EXPECT_NEAR(client.Position().Z, authority.Position().Z, 1e-4f);
}

// A request whose start the authority corrected carries a replay from the
// corrected start.
TEST(AnimRootMotion, AReplayIsCarriedFromACorrectedStart)
{
    RootMotionFixture authority(false);
    authority.StepTo(34);
    ASSERT_TRUE(authority.Dash().Accepted());
    authority.StepTo(60);

    RootMotionFixture client(false);
    client.StepTo(29);
    ASSERT_TRUE(client.Dash().Accepted()) << "guessed five ticks early";
    const RootMotionFixture::Saved at29 = client.Save();
    client.StepTo(60);

    AnimRequestSet& requests = *client.Entities.TryGet<AnimRequestSet>(client.Character);
    requests.Records[0].StartTick = 35;
    client.Restore(at29);
    client.Replay(30, 60);
    EXPECT_NEAR(client.Position().Z, authority.Position().Z, 1e-4f);
}

// Through the schedule a game composes -- abilities, movement, then
// animation -- a dash reaches the motion request the mover reads.
// A machine presenting no pose still runs every stage root motion needs, and skips a
// cosmetic rig beside it.
TEST(AnimRootMotion, TheScheduledPipelineCarriesTheCharacter)
{
    RootMotionFixture carried(false);
    (void)carried.Load("asset://anim/prop.behaviors.sdata", kAnimBehaviorSetType,
                       R"({ "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" } ] })");
    (void)carried.Load("asset://anim/prop.slots.sdata", kAnimSlotMapType,
                       R"({ "rows": [ { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" } ] })");
    const DataAssetHandle propRig = carried.Load("asset://anim/prop.rig.sdata", kAnimRigType, R"({
        "behaviors": [ "asset://anim/prop.behaviors.sdata" ], "slot_maps": [ "asset://anim/prop.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })");
    ASSERT_FALSE(carried.Bound(propRig).DrivesGameplay);
    ASSERT_TRUE(carried.Bound(carried.Rig).DrivesGameplay);
    const EntityId prop = carried.Entities.CreateEntity();
    carried.Entities.AddComponent(prop, AnimRig{ propRig });
    EngineConfig config;
    RuntimeFrameLoop runtime;
    DataAssetCache assets;
    EngineSchedule schedule;
    RegisterAbilityKitSystems(schedule);
    RegisterMovementSystems(schedule, assets);
    RegisterAnimationSystems(schedule, nullptr, AnimationHost{ .PresentsPose = false });
    schedule.Init();

    const auto run = [&](AnimTick tick) {
        FixedLogicContext context{
            .Config = config,
            .Runtime = runtime,
            .Time = FixedSimTime{ .DeltaSeconds = AnimRigFixture::kTick, .TickIndex = tick },
            .Entities = carried.Entities,
            .Partitions = Partitions(),
        };
        schedule.RunFixedLogic(context);
    };
    for (AnimTick tick = 0; tick < 5; ++tick)
        run(tick);
    AnimRequestDesc desc;
    desc.Source = carried.Character;
    desc.Intent = carried.Tag("anim.intent.dash");
    ASSERT_TRUE(IssueAnimRequest(carried.Entities, carried.Character, desc, 5).Accepted());
    run(5);
    run(6);
    const MotionRequest* request = static_cast<const World&>(carried.Entities).TryGet<MotionRequest>(carried.Character);
    ASSERT_NE(request, nullptr);
    EXPECT_NEAR(request->Velocity.Z, -3.0f, 1e-3f) << "3 m over a second, at the tick's rate";
    EXPECT_FALSE(carried.Playing(prop).Behavior.IsValid()) << "the cosmetic prop was never resolved";
}
