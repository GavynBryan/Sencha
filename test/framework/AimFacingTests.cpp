#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <camera/CameraRig.h>
#include <controller/AimFacingSystem.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <numbers>

//=============================================================================
// A body that faces its aim.
//
// The mechanism is one line of arithmetic; what these guard is the contract
// around it. Which entities it claims, which it must leave alone, that it turns
// a body the same way the camera turns a view for the same aim, and that
// claiming rotation does not mean claiming the rest of a transform.
//=============================================================================

namespace
{
    struct FacingHarness
    {
        FacingHarness()
        {
            WorldState.RegisterComponent<LookOrientation>();
            WorldState.RegisterComponent<AimFacing>();
            WorldState.RegisterComponent<LocalTransform>();
            Partitions.Add(StoragePartitionId::Default());

            Schedule.Register<AimFacingSystem>();
            Schedule.Init();
        }

        // An entity that aims, optionally one whose body turns with it.
        EntityId Aimer(float yaw, bool facesAim,
                       StoragePartitionId partition = StoragePartitionId::Default())
        {
            const EntityId entity = WorldState.CreateEntity(partition);
            LookOrientation look;
            look.Yaw = yaw;
            WorldState.AddComponent<LookOrientation>(entity, look);
            WorldState.AddComponent<LocalTransform>(entity, {});
            if (facesAim)
                WorldState.AddComponent<AimFacing>(entity, {});
            return entity;
        }

        void Tick()
        {
            FixedLogicContext fixed{
                .Config = Config,
                .Runtime = Runtime,
                .Time = FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = TickIndex },
                .Entities = WorldState,
                .Partitions = Partitions,
                .TicksLeftInFrame = 1,
            };
            Schedule.RunFixedLogic(fixed);
            ++TickIndex;
        }

        [[nodiscard]] Transform3f Pose(EntityId entity) const
        {
            return WorldState.TryGet<LocalTransform>(entity)->Value;
        }

        EngineConfig Config;
        RuntimeFrameLoop Runtime;
        World WorldState;
        StoragePartitionSet Partitions;
        EngineSchedule Schedule;
        std::uint64_t TickIndex = 1;
    };

    // Quaternions are equal up to sign; comparing the rotation they produce is
    // what actually matters and says so when it fails.
    void ExpectFaces(const Quatf& rotation, float yaw)
    {
        const Vec3d turned =
            rotation.RotateVector(Vec3d::Forward());
        const Vec3d expected =
            Quatf::FromAxisAngle(Vec3d::Up(), yaw).RotateVector(Vec3d::Forward());
        EXPECT_NEAR(turned.X, expected.X, 1e-5f);
        EXPECT_NEAR(turned.Y, expected.Y, 1e-5f);
        EXPECT_NEAR(turned.Z, expected.Z, 1e-5f);
    }
}

TEST(AimFacing, ABodyThatFacesItsAimTurnsToIt)
{
    FacingHarness harness;
    const EntityId body = harness.Aimer(0.75f, true);

    harness.Tick();

    ExpectFaces(harness.Pose(body).Rotation, 0.75f);
}

// The aim is a running total, not an angle folded into a circle, and the camera
// builds its own pose from the raw value. A body that wrapped it would face a
// hair away from the view it shares an aim with, once round.
TEST(AimFacing, AnAimPastAFullTurnIsTakenAsItStands)
{
    FacingHarness harness;
    const float wound = 2.0f * std::numbers::pi_v<float> + 0.4f;
    const EntityId body = harness.Aimer(wound, true);

    harness.Tick();

    ExpectFaces(harness.Pose(body).Rotation, wound);
}

// The contract worth having: for one aim, a body and the camera watching it
// point the same way. Two readers of one orientation that disagreed would be
// two orientations.
TEST(AimFacing, ABodyAndItsCameraAgreeOnWhereTheAimPoints)
{
    FacingHarness harness;
    constexpr float kYaw = -1.9f;
    const EntityId body = harness.Aimer(kYaw, true);

    harness.Tick();

    CameraRig rig{};
    rig.Mode = CameraRigMode::FirstPerson;
    rig.Target = body;
    const CameraPose view = ComputeCameraPose(rig, Vec3d::Zero(), kYaw, 0.0f);

    const Vec3d bodyForward =
        harness.Pose(body).Rotation.RotateVector(Vec3d::Forward());
    const Vec3d viewForward = view.Rotation.RotateVector(Vec3d::Forward());
    EXPECT_NEAR(bodyForward.X, viewForward.X, 1e-5f);
    EXPECT_NEAR(bodyForward.Z, viewForward.Z, 1e-5f);
}

// Aiming is not facing unless something says so. A third-person character aims
// where the player points and faces where it is running.
TEST(AimFacing, AnEntityWithoutTheTagIsLeftAlone)
{
    FacingHarness harness;
    const EntityId body = harness.Aimer(0.75f, false);

    harness.Tick();

    const Quatf rotation = harness.Pose(body).Rotation;
    EXPECT_NEAR(rotation.X, 0.0f, 1e-6f);
    EXPECT_NEAR(rotation.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(rotation.Z, 0.0f, 1e-6f);
    EXPECT_NEAR(std::abs(rotation.W), 1.0f, 1e-6f);
}

// Yaw only. A body pitched by its own look would lie on its back to look up;
// the vertical belongs to a lens or an animated spine.
TEST(AimFacing, PitchDoesNotTipTheBody)
{
    FacingHarness harness;
    const EntityId body = harness.Aimer(0.0f, true);
    harness.WorldState.TryGet<LookOrientation>(body)->Pitch = 0.9f;

    harness.Tick();

    const Vec3d up = harness.Pose(body).Rotation.RotateVector(Vec3d::Up());
    EXPECT_NEAR(up.X, 0.0f, 1e-5f);
    EXPECT_NEAR(up.Y, 1.0f, 1e-5f);
    EXPECT_NEAR(up.Z, 0.0f, 1e-5f);
}

// The tag claims the rotation and nothing else. A turret is placed with the
// proportions that tell it apart from a player, and turning it must not undo
// them.
TEST(AimFacing, TheRestOfTheTransformIsUntouched)
{
    FacingHarness harness;
    const EntityId body = harness.Aimer(0.5f, true);
    LocalTransform& placed = *harness.WorldState.TryGet<LocalTransform>(body);
    placed.Value.Position = Vec3d(3.0f, 2.0f, -4.0f);
    placed.Value.Scale = Vec3d(1.2f, 0.4f, 1.2f);

    harness.Tick();

    const Transform3f pose = harness.Pose(body);
    EXPECT_EQ(pose.Position, Vec3d(3.0f, 2.0f, -4.0f));
    EXPECT_EQ(pose.Scale, Vec3d(1.2f, 0.4f, 1.2f));
}

TEST(AimFacing, EveryTaggedBodyTurnsAndOnlyTaggedOnesDo)
{
    FacingHarness harness;
    const EntityId first = harness.Aimer(0.3f, true);
    const EntityId second = harness.Aimer(-1.1f, true);
    const EntityId bystander = harness.Aimer(2.2f, false);

    harness.Tick();

    ExpectFaces(harness.Pose(first).Rotation, 0.3f);
    ExpectFaces(harness.Pose(second).Rotation, -1.1f);
    ExpectFaces(harness.Pose(bystander).Rotation, 0.0f);
}

// Systems run over the partitions the frame made active. A body in a zone this
// frame is not simulating is not turned by it.
TEST(AimFacing, ABodyOutsideTheActivePartitionsIsNotTurned)
{
    FacingHarness harness;
    const EntityId resident = harness.Aimer(0.6f, true);
    const EntityId dormant =
        harness.Aimer(0.6f, true, StoragePartitionId{ 7 });

    harness.Tick();

    ExpectFaces(harness.Pose(resident).Rotation, 0.6f);
    ExpectFaces(harness.Pose(dormant).Rotation, 0.0f);
}

// A world where nothing aims is the ordinary case for most of a frame.
TEST(AimFacing, AWorldWithNothingToTurnIsFine)
{
    FacingHarness harness;
    EXPECT_NO_FATAL_FAILURE(harness.Tick());
}
