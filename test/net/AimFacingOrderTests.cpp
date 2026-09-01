#include <gtest/gtest.h>
#include <world/WorldComponentSchemas.h>

#include <app/EngineSchedule.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputFrame.h>
#include <movement/MovementIntent.h>
#include <net/ClientPrediction.h>
#include <net/NetPlayerCommand.h>
#include <net/NetReplicationComponents.h>
#include <net/NetTickEstimator.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationInterpolation.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cstddef>
#include <cstdint>

//=============================================================================
// Where turning a body to its aim sits among the other writers of a pose.
//
// A body that faces its aim is written by two machines for two reasons, and
// which of them is right depends on whose entity it is. The machine driving it
// turns it from the aim it is integrating, because waiting for a round trip to
// see your own gun move is the thing prediction exists to avoid. A machine only
// watching turns it from what the authority said, because it has no idea where
// that player is pointing until told.
//
// Neither is a decision the facing system makes -- it writes the same rotation
// on every machine and never asks about a network. What separates the two is
// ordering, declared where the net systems are registered, and this is that
// contract under the schedule that enforces it.
//=============================================================================

namespace
{
    // Far enough from the aim below that no tolerance could confuse them.
    constexpr float kAuthorityYaw = 1.2f;
    constexpr float kLocalAim = -2.5f;

    LocalTransform PoseFacing(float yaw)
    {
        LocalTransform pose;
        pose.Value.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), yaw);
        return pose;
    }

    // Compared as the direction the body ends up pointing rather than as an
    // angle: two quaternions that turn a body the same way are the same facing,
    // whatever their components say.
    void ExpectFacing(const World& world, EntityId entity, float yaw,
                      const char* what)
    {
        const Vec3d actual =
            world.TryGet<LocalTransform>(entity)->Value.Rotation.RotateVector(
                Vec3d::Forward());
        const Vec3d expected =
            Quatf::FromAxisAngle(Vec3d::Up(), yaw).RotateVector(Vec3d::Forward());
        EXPECT_NEAR(actual.X, expected.X, 1e-4f) << what;
        EXPECT_NEAR(actual.Z, expected.Z, 1e-4f) << what;
    }

    struct OrderHarness
    {
        OrderHarness()
        {
            RegisterControllerComponents(WorldState);
            WorldState.RegisterComponent<LocalTransform>();
            // Command capture asks every driven subject whether it means to
            // move, and asking about a type a world never registered is a
            // fault rather than a no.
            WorldState.RegisterComponent<MovementIntent>();
            // Registered up front rather than where the authority case needs
            // them: a world takes its component set before it has entities.
            WorldState.RegisterComponent<NetOwner>();
            WorldState.RegisterComponent<InputActionSourceRef>();
            Partitions.Add(StoragePartitionId::Default());

            Body = WorldState.CreateEntity();
            LookOrientation aim;
            aim.Yaw = kLocalAim;
            WorldState.AddComponent<LookOrientation>(Body, aim);
            WorldState.AddComponent<AimFacing>(Body, {});
            WorldState.AddComponent<LocalTransform>(Body, {});

            // The authority's word about where this body points, held the way a
            // pair of snapshots would leave it.
            Interpolation.Commit(Body, 1, PoseFacing(kAuthorityYaw));
            Interpolation.Commit(Body, 2, PoseFacing(kAuthorityYaw));
            Clock.Observe(100, 100, 0, 1.0 / 60.0);

            // The order the engine documents, and the order a game registers
            // them in: controller first, so the net edges have something to name.
            RegisterControllerSystems(Schedule);
            RegisterNetSystems(Schedule, Commands, Prediction, Interpolation, Clock);
            Schedule.Init();
        }

        void Tick()
        {
            FixedLogicContext fixed{
                .Config = Config,
                .Runtime = Runtime,
                .Time = FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = 120 },
                .Entities = WorldState,
                .Partitions = Partitions,
                .TicksLeftInFrame = 1,
            };
            Schedule.RunFixedLogic(fixed);
        }

        EngineConfig Config;
        RuntimeFrameLoop Runtime;
        World WorldState;
        StoragePartitionSet Partitions;
        EngineSchedule Schedule;
        PeerCommandRuntime Commands;
        ClientPrediction Prediction;
        ReplicationInterpolation Interpolation;
        NetTickEstimator Clock;
        EntityId Body;
    };
}

// Somebody else's player, mirrored. This machine has an aim on it only because
// the aim replicates; the pose it must draw is the authority's, arriving
// smoothed. Facing still runs -- it costs a quaternion, and covers the window
// before any sample has landed -- but it must not be what survives the tick.
TEST(AimFacingOrder, AWatchedBodyKeepsTheAuthoritysFacing)
{
    OrderHarness harness;

    harness.Tick();

    ExpectFacing(harness.WorldState, harness.Body, kAuthorityYaw,
                 "a locally derived facing outlived the authority's on a mirror");
}

// The body this machine drives. The predictor owns its pose and interpolation
// lets go of it, so the aim being integrated here is what turns it -- this tick,
// not a round trip later.
TEST(AimFacingOrder, TheDrivenBodyTurnsToTheAimBeingIntegratedHere)
{
    OrderHarness harness;
    harness.Prediction.SetPredicted(harness.Body);

    harness.Tick();

    ExpectFacing(harness.WorldState, harness.Body, kLocalAim,
                 "the driver waited for the authority to say where they were aiming");
}

// An authority turning a remote player's body. Their aim arrives in a command
// and is put on their entity by the feed, so facing has to come after it: this
// is the one edge registration order alone would get wrong, because the
// controller systems are registered first and would otherwise turn the body to
// the aim of the tick before.
TEST(AimFacingOrder, AnAuthorityTurnsARemoteBodyToTheAimThatJustArrived)
{
    constexpr PeerId kDriver{ 3 };
    constexpr float kSentYaw = 0.9f;

    OrderHarness harness;
    // Not this machine's own body, and not one it mirrors: a body it owns the
    // simulation of on somebody else's behalf.
    harness.Interpolation.Forget(harness.Body);
    harness.WorldState.AddComponent<NetOwner>(
        harness.Body, NetOwner{ .Peer = kDriver.Value });
    harness.WorldState.AddComponent<InputActionSourceRef>(
        harness.Body, InputActionSourceRef{ .Source = kDriver.Value });
    harness.WorldState.AddResource<InputActionState>().Configure(1);

    NetPlayerCommand command;
    command.RecordCount = 1;
    command.Records[0].Tick = 120;
    command.Records[0].Yaw = kSentYaw;
    command.Records[0].ActionCount = 1;

    std::array<std::byte, 256> bytes{};
    bytes[0] = static_cast<std::byte>(NetPayloadKind::Command);
    NetBitWriter writer(std::span<std::byte>(bytes).subspan(1));
    const std::size_t encoded = NetEncodePlayerCommand(command, writer);
    ASSERT_GT(encoded, 0u);
    ASSERT_TRUE(harness.Commands.Receive(
        kDriver, std::span<const std::byte>(bytes).subspan(
                     0, 1 + writer.BytesWritten())));

    harness.Tick();

    ExpectFacing(harness.WorldState, harness.Body, kSentYaw,
                 "the body faces the aim of the tick before the one that arrived");
}
