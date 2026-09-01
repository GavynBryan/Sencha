#include <gtest/gtest.h>
#include <world/transform/TransformComponentSchemas.h>

#include <app/EngineSchedule.h>
#include <camera/CameraFollowSystem.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionResolve.h>
#include <input/InputActionState.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    // A character reads its own aim during simulation to steer along it. This
    // stands in for the game's own input system, which lives in game code and
    // cannot be linked here.
    struct YawReadingSystem
    {
        EntityId Pawn;
        float ObservedYaw = 0.0f;
        int Ticks = 0;
        // Every tick's aim, not just the newest: a frame that runs several must
        // be checkable tick by tick.
        std::vector<float> ObservedYaws;

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (const LookOrientation* look = ctx.Entities.TryGet<LookOrientation>(Pawn))
            {
                ObservedYaw = look->Yaw;
                ObservedYaws.push_back(look->Yaw);
                ++Ticks;
            }
        }
    };

    struct LookHarness
    {
        LookHarness()
        {
            WorldState.RegisterComponent<CameraRig>();
            WorldState.RegisterComponent<LookOrientation>();
            WorldState.RegisterComponent<LocalLookControl>();
            WorldState.RegisterComponent<LocalTransform>();
            WorldState.RegisterComponent<WorldTransform>();
            // The camera follows the pose its target is drawn at, so placement
            // consults the target's interpolation history.
            WorldState.RegisterComponent<WorldTransformHistory>();
            Partitions.Add(StoragePartitionId::Default());

            // The pawn aims; the camera follows it and presents that aim.
            Pawn = WorldState.CreateEntity();
            WorldState.AddComponent<LookOrientation>(Pawn, {});
            WorldState.AddComponent<LocalLookControl>(Pawn, {});
            WorldState.AddComponent<LocalTransform>(Pawn, {});

            Camera = WorldState.CreateEntity();
            CameraRig rig{};
            rig.Mode = CameraRigMode::FirstPerson;
            rig.Target = Pawn;
            WorldState.AddComponent<CameraRig>(Camera, rig);
            WorldState.AddComponent<LocalTransform>(Camera, {});
            WorldState.AddResource<ActiveCameraService>().SetActive(Camera);

            // Aim integrates from a resolved look action. Filling the snapshot
            // directly keeps this focused: the mapper has its own coverage, and
            // booting it here would prove nothing extra.
            WorldState.AddResource<InputActionState>().Configure(1);
            WorldState.AddResource<LookInputBinding>().Look = InputActionId{ 1 };

            Integrate = &Schedule.Register<LookIntegrationSystem>();
            Follow = &Schedule.Register<CameraFollowSystem>();
            Reader = &Schedule.Register<YawReadingSystem>();
            Reader->Pawn = Pawn;
            Schedule.Init();
        }

        // One rendered frame in engine order, running the tick count the
        // accumulator asked for: look accumulation on the frame snapshot, the
        // ticks that consume it, then placement.
        //
        // The real latches do the splitting, so this exercises the composition
        // rather than a hand-rolled model of it. The look value is angular
        // displacement for the frame: the binding's scale has already turned
        // device units into radians by this point.
        void RunFrameWithTicks(std::uint32_t ticks, float lookX, float lookY = 0.0f)
        {
            InputActionState& state = WorldState.GetResource<InputActionState>();

            // The mapper folds the frame's displacement into both clocks.
            Presentation.MotionX += lookX;
            Presentation.MotionY += lookY;
            Simulation.MotionX += lookX;
            Simulation.MotionY += lookY;

            // Presentation resolves once per frame and takes everything.
            const InputMotionDelta framePass = Presentation.Share(1);
            Presentation.Consume(framePass);
            state.FrameStorage()[0] =
                InputActionValue{ framePass.X, framePass.Y, InputActionFlags::None };

            PreSimulateContext preSimulate{
                .Config = Config,
                .Runtime = Runtime,
                .Input = Input,
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunPreSimulate(preSimulate);

            for (std::uint32_t i = 0; i < ticks; ++i)
            {
                // Ticks left including this one, so the last takes the exact
                // remainder however the division rounded.
                const std::uint32_t left = ticks - i;
                const InputMotionDelta tickPass = Simulation.Share(left);
                Simulation.Consume(tickPass);
                state.BeginTick(TickIndex)[0] =
                    InputActionValue{ tickPass.X, tickPass.Y, InputActionFlags::None };

                FixedLogicContext fixed{
                    .Config = Config,
                    .Runtime = Runtime,
                    .Time = {},
                    .Entities = WorldState,
                    .Partitions = Partitions,
                    .TicksLeftInFrame = left,
                };
                Schedule.RunFixedLogic(fixed);
                ++TickIndex;
            }

            FrameUpdateContext frame{
                .Config = Config,
                .Runtime = Runtime,
                .Input = Input,
                .WallDeltaSeconds = 1.0 / 60.0,
                .Presentation = {},
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunFrameUpdate(frame);
        }

        void RunFrame(float lookX, float lookY = 0.0f)
        {
            RunFrameWithTicks(1, lookX, lookY);
        }

        // One rendered frame driven by wall time, holding a stick at a fixed
        // deflection: the accumulator decides the tick count and the camera
        // gets the real alpha, which is what the rate lead is derived from.
        // `stickX` is the conditioned per-second value the resolve publishes
        // on both the mixed and the sampled channel.
        void RunWallFrame(double dt, float stickX)
        {
            constexpr double fixedDt = 1.0 / 60.0;
            InputActionState& state = WorldState.GetResource<InputActionState>();

            state.FrameStorage()[0] =
                InputActionValue{ stickX, 0.0f, InputActionFlags::None };
            state.FrameSampledStorage()[0] =
                InputActionValue{ stickX, 0.0f, InputActionFlags::None };

            PreSimulateContext preSimulate{
                .Config = Config,
                .Runtime = Runtime,
                .Input = Input,
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunPreSimulate(preSimulate);

            Residual += dt;
            std::uint32_t ticks = 0;
            while (Residual >= fixedDt)
            {
                Residual -= fixedDt;
                ++ticks;
            }
            for (std::uint32_t i = 0; i < ticks; ++i)
            {
                state.BeginTick(TickIndex)[0] =
                    InputActionValue{ stickX, 0.0f, InputActionFlags::None };
                state.TickSampledStorage()[0] =
                    InputActionValue{ stickX, 0.0f, InputActionFlags::None };

                FixedLogicContext fixed{
                    .Config = Config,
                    .Runtime = Runtime,
                    .Time = FixedSimTime{ .DeltaSeconds = fixedDt,
                                          .TickIndex = TickIndex },
                    .Entities = WorldState,
                    .Partitions = Partitions,
                    .TicksLeftInFrame = ticks - i,
                };
                Schedule.RunFixedLogic(fixed);
                ++TickIndex;
            }

            FrameUpdateContext frame{
                .Config = Config,
                .Runtime = Runtime,
                .Input = Input,
                .WallDeltaSeconds = dt,
                .Presentation = PresentationTime{ .DeltaSeconds = fixedDt,
                                                  .Alpha = Residual / fixedDt },
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunFrameUpdate(frame);
        }

        // Where the camera actually aimed this frame: the yaw of the pose the
        // follow pass wrote, which is the thing the player sees.
        [[nodiscard]] float CameraYaw() const
        {
            const LocalTransform* transform =
                WorldState.TryGet<LocalTransform>(Camera);
            const Vec3d forward =
                transform->Value.Rotation.RotateVector(Vec3d::Forward());
            return std::atan2(-forward.X, -forward.Z);
        }

        // The aim simulation steers along.
        [[nodiscard]] float Yaw() const
        {
            return WorldState.TryGet<LookOrientation>(Pawn)->Yaw;
        }

        // The aim the view is presented at: the simulation aim plus whatever
        // the pointer has moved since the last tick consumed anything.
        [[nodiscard]] float PresentedYaw() const
        {
            const float lead = WorldState.TryGetResource<PendingLookInput>() != nullptr
                ? WorldState.TryGetResource<PendingLookInput>()->Yaw
                : 0.0f;
            return Yaw() + lead;
        }

        EngineConfig Config;
        RuntimeFrameLoop Runtime;
        InputFrame Input;
        World WorldState;
        StoragePartitionSet Partitions;
        EngineSchedule Schedule;
        EntityId Camera;
        EntityId Pawn;
        LookIntegrationSystem* Integrate = nullptr;
        CameraFollowSystem* Follow = nullptr;
        YawReadingSystem* Reader = nullptr;

        // The two clocks' unconsumed displacement, as the mapper carries it.
        InputEdgeLatch Presentation;
        InputEdgeLatch Simulation;
        std::uint64_t TickIndex = 1;
        // Wall time the fixed-step boundary has not crossed yet, for the
        // wall-clock stepper: what the engine's accumulator carries.
        double Residual = 0.0;
    };
}

// The defect this guards: look accumulation used to run in FrameUpdate, after
// simulation, so a tick steered along the previous frame's orientation while the
// same frame rendered the new one. Turning and moving at once then carried
// velocity along a heading the player had already left.
TEST(CameraLook, SimulationSeesTheSameFrameYaw)
{
    LookHarness harness;
    harness.RunFrame(0.1f);

    const float expected = -0.1f;
    EXPECT_EQ(harness.Reader->Ticks, 1);
    EXPECT_FLOAT_EQ(harness.Reader->ObservedYaw, expected)
        << "the tick must steer along this frame's look, not the last frame's";
    EXPECT_FLOAT_EQ(harness.Yaw(), expected);
}

// Splitting the system across two phases must not apply the same mouse delta
// twice; placement reads the orientation, it does not accumulate it.
TEST(CameraLook, PlacementDoesNotAccumulateLookAgain)
{
    LookHarness harness;
    harness.RunFrame(0.04f);
    const float afterFirst = harness.Yaw();
    EXPECT_FLOAT_EQ(afterFirst, -0.04f);

    // A frame with no look input leaves the orientation where it was.
    harness.RunFrame(0.0f);
    EXPECT_FLOAT_EQ(harness.Yaw(), afterFirst);
    EXPECT_FLOAT_EQ(harness.Reader->ObservedYaw, afterFirst);
}

TEST(CameraLook, PitchStaysClampedWhenAccumulatedBeforeSimulation)
{
    LookHarness harness;
    LookOrientation* look = harness.WorldState.TryGet<LookOrientation>(harness.Pawn);
    look->MinPitch = -0.5f;
    look->MaxPitch = 0.5f;

    harness.RunFrame(0.0f, -1000.0f);

    EXPECT_FLOAT_EQ(harness.WorldState.TryGet<LookOrientation>(harness.Pawn)->Pitch, 0.5f);
}

// The jitter this guards: the aim used to integrate once per rendered frame
// while movement rebuilt its heading once per fixed tick. When the display rate
// and the tick rate are close, the accumulator runs zero, one, or two ticks on
// alternating frames, so a frame that ran none turned the heading for free and a
// frame that ran two stepped twice under one angle. Steering along that heading
// while moving made the velocity direction lurch. Every tick must advance the
// aim by the displacement covering the span it simulates.
TEST(CameraLook, EachTickTurnsByTheTravelSinceTheLastTick)
{
    LookHarness harness;
    constexpr float perFrame = 0.03f;

    // The tick pattern a 60 Hz display produces against a 60 Hz simulation.
    const std::uint32_t pattern[] = { 1, 0, 2, 1 };

    for (std::uint32_t ticks : pattern)
    {
        const float before = harness.Yaw();
        harness.RunFrameWithTicks(ticks, perFrame);
        if (ticks == 0)
        {
            EXPECT_FLOAT_EQ(harness.Yaw(), before)
                << "a frame that ran no tick must not turn the heading";
        }
    }

    // Four frames of travel spread evenly over the four ticks that ran, so the
    // heading advances by the same angle for every equal step of simulated
    // time. Before the fix these came out -0.03, -0.09, -0.09, -0.12.
    const std::vector<float>& observed = harness.Reader->ObservedYaws;
    ASSERT_EQ(observed.size(), 4u);
    for (std::size_t i = 0; i < observed.size(); ++i)
    {
        EXPECT_NEAR(observed[i], -perFrame * static_cast<float>(i + 1), 1e-5f)
            << "tick " << i << " must sit one frame of travel past the last";
    }
}

// A catch-up frame simulates two ticks. Both stepping under one angle is the
// defect; the second must aim one tick further round than the first.
TEST(CameraLook, TicksInOneFrameDoNotShareAnAngle)
{
    LookHarness harness;
    harness.RunFrameWithTicks(0, 0.02f);
    const float beforeBurst = harness.Yaw();

    // Two frames of travel are outstanding and two ticks run: each takes one.
    harness.RunFrameWithTicks(2, 0.02f);

    const std::vector<float>& observed = harness.Reader->ObservedYaws;
    ASSERT_EQ(observed.size(), 2u);
    EXPECT_NE(observed[0], observed[1])
        << "two ticks of one frame must not simulate under the same aim";
    EXPECT_NEAR(observed[0], beforeBurst - 0.02f, 1e-5f);
    EXPECT_NEAR(observed[1], beforeBurst - 0.04f, 1e-5f);
}

// Moving the aim onto the tick clock must not make the view step at the tick
// rate: a frame that runs no tick still turns what the player sees.
TEST(CameraLook, ViewTurnsOnAFrameThatRunsNoTick)
{
    LookHarness harness;
    harness.RunFrameWithTicks(1, 0.05f);
    const float settled = harness.Yaw();

    harness.RunFrameWithTicks(0, 0.05f);

    EXPECT_FLOAT_EQ(harness.Yaw(), settled)
        << "no simulated time passed, so the heading must not move";
    EXPECT_NEAR(harness.PresentedYaw(), settled - 0.05f, 1e-5f)
        << "the view must still track the pointer at frame rate";
}

// The lead is what simulation has not caught up to, never a running total: once
// a tick absorbs it the view must not count it a second time.
TEST(CameraLook, TheViewLeadIsSpentByTheNextTick)
{
    LookHarness harness;
    harness.RunFrameWithTicks(0, 0.05f);
    harness.RunFrameWithTicks(1, 0.0f);

    EXPECT_NEAR(harness.Yaw(), -0.05f, 1e-5f);
    EXPECT_FLOAT_EQ(harness.PresentedYaw(), harness.Yaw())
        << "the tick consumed the lead, so the view and the aim agree";
}

// A stick is a sample, not a displacement: the same deflection reads on every
// pass. Accumulating it per frame while ticks consumed it per tick stepped the
// view backward every time a tick landed -- at a 144 Hz display against a
// 60 Hz simulation, a vibration at tick rate. A held stick must turn the view
// by rate times wall time, every frame, in one direction.
TEST(CameraLook, AHeldStickTurnsTheViewWithoutSteppingBack)
{
    LookHarness harness;
    constexpr double dt = 1.0 / 144.0;
    constexpr float rate = 2.4f;

    harness.RunWallFrame(dt, rate);
    float previous = harness.CameraYaw();

    int reversals = 0;
    double totalStep = 0.0;
    double worstStep = 0.0;
    const double expectedStep = rate * dt;
    for (int i = 0; i < 90; ++i)
    {
        harness.RunWallFrame(dt, rate);
        const double step = static_cast<double>(harness.CameraYaw()) - previous;
        previous = harness.CameraYaw();
        if (step > 0.0)
            ++reversals;
        totalStep += step;
        worstStep = std::max(worstStep, std::abs(std::abs(step) - expectedStep));
    }

    EXPECT_EQ(reversals, 0)
        << "the view must never move against a steadily held stick";
    EXPECT_LT(worstStep, expectedStep * 0.02)
        << "every frame advances by rate times its wall time";
    EXPECT_NEAR(totalStep, -rate * dt * 90.0, 1e-3);
}

// Turn speed is a property of the stick, not of the display: a second of wall
// time turns the view by the same angle at 144 Hz and 60 Hz frame rates.
TEST(CameraLook, StickTurnSpeedIsIndependentOfFrameRate)
{
    constexpr float rate = 2.4f;

    LookHarness at144;
    for (int i = 0; i < 144; ++i)
        at144.RunWallFrame(1.0 / 144.0, rate);

    LookHarness at60;
    for (int i = 0; i < 60; ++i)
        at60.RunWallFrame(1.0 / 60.0, rate);

    EXPECT_NEAR(at144.CameraYaw(), at60.CameraYaw(), 1e-3f);
    EXPECT_NEAR(at144.CameraYaw(), -rate, 5e-3f)
        << "one second at full deflection turns one rate's worth";
}

// The simulation's own aim advances only with simulated time: a frame that
// runs two ticks turns the heading by two ticks of rate, a frame that runs
// none leaves it alone.
TEST(CameraLook, StickAdvancesTheHeadingByRateTimesSimulatedTime)
{
    LookHarness harness;
    constexpr double fixedDt = 1.0 / 60.0;
    constexpr float rate = 2.4f;

    // Two frames of wall time, no tick boundary crossed yet.
    harness.RunWallFrame(fixedDt * 0.4, rate);
    EXPECT_FLOAT_EQ(harness.Yaw(), 0.0f)
        << "no simulated time passed, so the heading must not move";

    // Enough wall time to cross two tick boundaries at once.
    harness.RunWallFrame(fixedDt * 2.0, rate);
    EXPECT_NEAR(harness.Yaw(), -rate * fixedDt * 2.0, 1e-5f);
}

TEST(CameraPose, FirstPersonSitsAtPivot)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;
    rig.PivotOffset = Vec3d(0.0f, 1.6f, 0.0f);

    const CameraPose pose = ComputeCameraPose(rig, Vec3d(5.0f, 0.0f, 3.0f), 0.0f, 0.0f);

    EXPECT_TRUE(pose.Override);
    EXPECT_FLOAT_EQ(pose.Position.X, 5.0f);
    EXPECT_FLOAT_EQ(pose.Position.Y, 1.6f);
    EXPECT_FLOAT_EQ(pose.Position.Z, 3.0f);
}

TEST(CameraPose, ThirdPersonPlacesBoomBehindAtRest)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::ThirdPerson;
    rig.PivotOffset = Vec3d(0.0f, 1.0f, 0.0f);
    rig.Distance = 4.0f;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d::Zero(), 0.0f, 0.0f);

    // At yaw 0 / pitch 0 the look direction is -Z, so the boom (behind) is +Z.
    EXPECT_TRUE(pose.Override);
    EXPECT_NEAR(pose.Position.X, 0.0f, 1e-4f);
    EXPECT_NEAR(pose.Position.Y, 1.0f, 1e-4f);
    EXPECT_NEAR(pose.Position.Z, 4.0f, 1e-4f);
}

TEST(CameraPose, ThirdPersonPreservesBoomLength)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::ThirdPerson;
    rig.PivotOffset = Vec3d::Zero();
    rig.Distance = 4.0f;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d::Zero(), 0.9f, 0.2f);

    EXPECT_NEAR(pose.Position.Magnitude(), rig.Distance, 1e-3f);
}

TEST(CameraPose, FixedLeavesAuthoredPose)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::Fixed;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d(9.0f, 9.0f, 9.0f), 0.0f, 0.0f);

    EXPECT_FALSE(pose.Override);
}

// A first-person camera sits at the pivot inside its target, so drawing the
// target fills the view with the inside of its own body.
TEST(CameraExclusion, FirstPersonExcludesItsTarget)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;
    rig.Target = EntityId{ 7, 2 };

    EXPECT_EQ(CameraRigExcludedEntity(rig), rig.Target);
}

// The boom looks at the target from outside it: seeing your own body is the
// point of the mode.
TEST(CameraExclusion, ThirdPersonExcludesNothing)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::ThirdPerson;
    rig.Target = EntityId{ 7, 2 };

    EXPECT_FALSE(CameraRigExcludedEntity(rig).IsValid());
}

TEST(CameraExclusion, FixedExcludesNothing)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::Fixed;
    rig.Target = EntityId{ 7, 2 };

    EXPECT_FALSE(CameraRigExcludedEntity(rig).IsValid());
}

// A rig with no target excludes nothing rather than excluding "entity zero":
// the exclusion is carried as an id, and an invalid id matches no live entity.
TEST(CameraExclusion, FirstPersonWithNoTargetExcludesNothing)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;

    EXPECT_FALSE(CameraRigExcludedEntity(rig).IsValid());
}

// Identity is generational: once a target's slot is recycled, the rig's stale id
// must stop matching the entity now living in that slot.
TEST(CameraExclusion, ExclusionDoesNotFollowARecycledSlot)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;
    rig.Target = EntityId{ 7, 2 };

    const EntityId recycled{ 7, 3 };
    EXPECT_NE(CameraRigExcludedEntity(rig), recycled);
}
