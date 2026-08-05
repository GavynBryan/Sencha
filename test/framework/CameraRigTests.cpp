#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <camera/CameraFollowSystem.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionState.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

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

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (const LookOrientation* look = ctx.Entities.TryGet<LookOrientation>(Pawn))
            {
                ObservedYaw = look->Yaw;
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

        // One rendered frame in engine order: look accumulation, then the fixed
        // tick that consumes it, then placement.
        //
        // The look value is angular displacement for the frame: the binding's
        // scale has already turned device units into radians by this point.
        void RunFrame(float lookX, float lookY = 0.0f)
        {
            WorldState.GetResource<InputActionState>().FrameStorage()[0] =
                InputActionValue{ lookX, lookY, InputActionFlags::None };

            PreSimulateContext preSimulate{
                .Config = Config,
                .Runtime = Runtime,
                .Input = Input,
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunPreSimulate(preSimulate);

            FixedLogicContext fixed{
                .Config = Config,
                .Runtime = Runtime,
                .Time = {},
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunFixedLogic(fixed);

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

        [[nodiscard]] float Yaw() const
        {
            return WorldState.TryGet<LookOrientation>(Pawn)->Yaw;
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
