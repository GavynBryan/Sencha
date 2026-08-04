#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <camera/CameraFollowSystem.h>
#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>

namespace
{
    // A character reads the rig's yaw during simulation to steer along it. This
    // stands in for the game's own input system, which lives in game code and
    // cannot be linked here.
    struct YawReadingSystem
    {
        float ObservedYaw = 0.0f;
        int Ticks = 0;

        void FixedLogic(FixedLogicContext& ctx)
        {
            const auto* cameras = ctx.Entities.TryGetResource<ActiveCameraService>();
            if (cameras == nullptr || !cameras->HasActive())
                return;
            if (const CameraRig* rig = ctx.Entities.TryGet<CameraRig>(cameras->GetActive()))
            {
                ObservedYaw = rig->Yaw;
                ++Ticks;
            }
        }
    };

    struct LookHarness
    {
        LookHarness()
        {
            WorldState.RegisterComponent<CameraRig>();
            WorldState.RegisterComponent<LocalTransform>();
            WorldState.RegisterComponent<WorldTransform>();
            Partitions.Add(StoragePartitionId::Default());

            Camera = WorldState.CreateEntity();
            CameraRig rig{};
            rig.Mode = CameraRigMode::FirstPerson;
            rig.Sensitivity = 0.01f;
            WorldState.AddComponent<CameraRig>(Camera, rig);
            WorldState.AddComponent<LocalTransform>(Camera, {});
            WorldState.AddResource<ActiveCameraService>().SetActive(Camera);

            Follow = &Schedule.Register<CameraFollowSystem>();
            Reader = &Schedule.Register<YawReadingSystem>();
            Schedule.Init();
        }

        // One rendered frame in engine order: look accumulation, then the fixed
        // tick that consumes it, then placement.
        void RunFrame(float mouseDeltaX)
        {
            Input.MouseDeltaX = mouseDeltaX;

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
                .Input = Input,
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
            return WorldState.TryGet<CameraRig>(Camera)->Yaw;
        }

        EngineConfig Config;
        RuntimeFrameLoop Runtime;
        InputFrame Input;
        World WorldState;
        StoragePartitionSet Partitions;
        EngineSchedule Schedule;
        EntityId Camera;
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
    harness.RunFrame(10.0f);

    const float expected = -10.0f * 0.01f;
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
    harness.RunFrame(4.0f);
    const float afterFirst = harness.Yaw();
    EXPECT_FLOAT_EQ(afterFirst, -4.0f * 0.01f);

    // A frame with no mouse motion leaves the orientation where it was.
    harness.RunFrame(0.0f);
    EXPECT_FLOAT_EQ(harness.Yaw(), afterFirst);
    EXPECT_FLOAT_EQ(harness.Reader->ObservedYaw, afterFirst);
}

TEST(CameraLook, PitchStaysClampedWhenAccumulatedBeforeSimulation)
{
    LookHarness harness;
    CameraRig* rig = harness.WorldState.TryGet<CameraRig>(harness.Camera);
    rig->MinPitch = -0.5f;
    rig->MaxPitch = 0.5f;

    harness.Input.MouseDeltaY = -1000.0f;
    harness.RunFrame(0.0f);

    EXPECT_FLOAT_EQ(harness.WorldState.TryGet<CameraRig>(harness.Camera)->Pitch, 0.5f);
}

TEST(CameraPose, FirstPersonSitsAtPivot)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;
    rig.PivotOffset = Vec3d(0.0f, 1.6f, 0.0f);

    const CameraPose pose = ComputeCameraPose(rig, Vec3d(5.0f, 0.0f, 3.0f));

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

    const CameraPose pose = ComputeCameraPose(rig, Vec3d::Zero());

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
    rig.Yaw = 0.9f;
    rig.Pitch = 0.2f;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d::Zero());

    EXPECT_NEAR(pose.Position.Magnitude(), rig.Distance, 1e-3f);
}

TEST(CameraPose, FixedLeavesAuthoredPose)
{
    CameraRig rig{};
    rig.Mode = CameraRigMode::Fixed;

    const CameraPose pose = ComputeCameraPose(rig, Vec3d(9.0f, 9.0f, 9.0f));

    EXPECT_FALSE(pose.Override);
}
