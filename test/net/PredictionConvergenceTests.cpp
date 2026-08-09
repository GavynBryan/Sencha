#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/JumpState.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponents.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <net/ClientPrediction.h>
#include <net/LoopbackTransport.h>
#include <net/NetPlayerCommand.h>
#include <net/NetProtocol.h>
#include <net/NetSession.h>
#include <net/NetTickEstimator.h>
#include <net/NetReplicationComponents.h>
#include <net/PawnStateReplay.h>
#include <net/PeerCommandRuntime.h>
#include <net/ReplicationRuntime.h>
#include <net/SimulatedTransport.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <cmath>
#include <cstdint>
#include <vector>

//=============================================================================
// The incident, as a test.
//
// A live session on a map with corners came apart: the authority had the pawn
// wedged against geometry while its own machine had it running down a hallway,
// and nothing ever pulled the two back together. Four defects stacked up to
// cause that, each fixed and pinned separately -- this is the test for the sum,
// because the sum is what players experience.
//
// Two complete machines, each with its own world, physics, movement systems,
// and character motor. A real session between them over a link that loses,
// delays, and reorders. The client presses into the wall for seconds at a time,
// exactly the shape that used to diverge without bound. The claim under test:
// however bad the link, the two machines' answers for the pawn are pulled back
// together faster than they drift apart, and when the input stops they agree.
//=============================================================================

namespace
{
    constexpr double kTickSeconds = 1.0 / 60.0;
    constexpr float kDt = 1.0f / 60.0f;
    const Vec3d kGravity{ 0.0f, -9.81f, 0.0f };
    const Vec3d kUpAxis{ 0.0f, 1.0f, 0.0f };

    // Far from any tick a client counts to on its own, so a stamp built on the
    // shared clock and one built on the local counter can never coincide.
    constexpr std::uint64_t kAuthorityEpoch = 4'000'000;

    constexpr InputActionId kMove{ 1 };
    constexpr InputActionId kJump{ 2 };
    constexpr std::size_t kActionCount = 2;

    NetIdentity ConvergenceIdentity()
    {
        NetIdentity identity;
        identity.ModuleFingerprint = 0xF00D;
        identity.WorldIdentity = 0xCAFE;
        identity.FixedTickRateMilliHz = 60000;
        return identity;
    }

    const StoragePartitionSet& ActivePartitions()
    {
        static const StoragePartitionSet partitions = [] {
            StoragePartitionSet value;
            value.Add(StoragePartitionId::Default());
            return value;
        }();
        return partitions;
    }

    // A floor and the wall the pawn is driven into. The wall is the incident:
    // approximate reconciliation agrees with the real thing on open ground and
    // falls apart against geometry that stops one machine and not the other.
    void BuildLevel(PhysicsWorld& physics)
    {
        BodyDesc floor;
        floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
        floor.Motion = BodyMotion::Static;
        floor.Layer = CollisionLayer::Static;
        (void)physics.AddBody(floor);

        BodyDesc wall;
        wall.Shape = CollisionShape::MakeBox(Vec3d(0.5f, 4.0f, 12.0f));
        wall.Motion = BodyMotion::Static;
        wall.Layer = CollisionLayer::Static;
        wall.Position = Vec3d(4.0f, 2.0f, 0.0f);
        (void)physics.AddBody(wall);
    }

    // One tick of raw player input, exactly what the wire carries.
    struct ScriptTick
    {
        float Forward = 0.0f;
        float Strafe = 0.0f;
        float Yaw = 0.0f;
        bool Jump = false;
    };

    // The game's input derivation, shared verbatim by both ends the way the
    // template's input system is shared by being the same compiled code.
    MovementIntent DeriveIntent(const ScriptTick& input)
    {
        const Quatf frame = Quatf::FromAxisAngle(Vec3d::Up(), input.Yaw);
        Vec3d wish = frame.RotateVector(Vec3d::Forward()) * input.Forward
                   + frame.RotateVector(Vec3d::Right()) * input.Strafe;
        wish.Y = 0.0f;
        const float squared = wish.SqrMagnitude();
        if (squared > 1.0f)
            wish = wish * (1.0f / std::sqrt(squared));

        MovementIntent intent;
        intent.WishDir = wish;
        intent.Jump = input.Jump;
        return intent;
    }

    MovementIntent DeriveIntentFromActions(const InputActionView& actions,
                                           float yaw)
    {
        ScriptTick input;
        const Vec2d move = actions.Axis2(kMove);
        input.Strafe = static_cast<float>(move.X);
        input.Forward = static_cast<float>(move.Y);
        input.Yaw = yaw;
        input.Jump = actions.Fired(kJump);
        return DeriveIntent(input);
    }

    // One complete simulating machine.
    struct SimWorld
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        PhysicsWorld Physics;
        World Entities;
        CharacterMoverPool Movers{ Physics };

        FreeLocomotionSystem Locomotion{ kGravity, kUpAxis };
        JumpExecutionSystem Jump;
        MotionCompositionSystem Composition;

        SimWorld()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
            RegisterMovement(Entities);
            Layout.Seal();
            BuildLevel(Physics);

            Entities.AddResource<InputActionState>().Configure(kActionCount);
        }

        void AddBody(EntityId pawn)
        {
            const auto ensure = [&]<typename T>(T value) {
                if (!Entities.HasComponent<T>(pawn))
                    Entities.AddComponent<T>(pawn, value);
            };
            ensure(CharacterController{});
            ensure(MovementIntent{});
            ensure(JumpState{});
            ensure(KinematicState{});
            ensure(SupportState{});
            ensure(ResolvedMovementTuning{});
            ensure(LocomotionOutput{});
            ensure(MotionAxisOverride{});
            ensure(MotionImpulse{});
            ensure(MotionRequest{});
            ensure(LookOrientation{});
            ensure(CharacterMovement{
                .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode(),
            });
            Movers.Reconcile(Entities, ActivePartitions());
        }

        // The movement chain in schedule order, for every pawn with a body.
        void TickMovement()
        {
            Locomotion.Step(Entities, kDt);
            Jump.Step(Entities, kDt);
            Composition.Step(Entities);
            Movers.Reconcile(Entities, ActivePartitions());
            Movers.Drive(Entities, ActivePartitions(), kDt, kGravity);
        }

        [[nodiscard]] Vec3d PositionOf(EntityId pawn) const
        {
            const LocalTransform* pose = Entities.TryGet<LocalTransform>(pawn);
            return pose == nullptr ? Vec3d::Zero() : pose->Value.Position;
        }
    };

    struct Endpoint
    {
        Endpoint(LoopbackNetwork& network, const NetImpairment& impairment)
            : Wire(network), Link(Wire, impairment), Session(Link)
        {
        }

        LoopbackTransport Wire;
        SimulatedTransport Link;
        NetSession Session;
    };

    //-------------------------------------------------------------------------
    // The whole loop: an authority and one client, both simulating, joined by
    // an impaired link, stepped a tick at a time in the frame's order.
    //-------------------------------------------------------------------------
    struct ConvergenceRig
    {
        LoopbackNetwork Network;
        Endpoint Host;
        Endpoint Client;

        SimWorld Authority;
        SimWorld Mirror;

        ReplicationRuntime HostReplication;
        ReplicationRuntime ClientReplication;
        PeerCommandRuntime HostCommands;
        PeerCommandRuntime ClientCommands;
        ClientPrediction Prediction;
        NetTickEstimator Clock;

        EntityId AuthorityPawn;
        EntityId ClientPawn;

        double Now = 0.0;
        std::uint64_t AuthorityTick = kAuthorityEpoch;
        std::uint64_t ClientTick = 0;
        bool ReplayEnabled = true;

        explicit ConvergenceRig(const NetImpairment& impairment)
            : Host(Network, impairment), Client(Network, impairment)
        {
            Clock.SetSlackTicks(
                static_cast<std::uint32_t>(NetPeerCommandBuffer::kTargetDepth));
        }

        [[nodiscard]] bool Join(int maxSteps)
        {
            if (!Host.Session.Host(0, ConvergenceIdentity()))
                return false;
            if (!Client.Session.Connect(Host.Session.LocalAddress(),
                                        ConvergenceIdentity()))
            {
                return false;
            }
            for (int step = 0; step < maxSteps && !Client.Session.IsConnected();
                 ++step)
            {
                Advance(ScriptTick{});
            }
            if (!Client.Session.IsConnected())
                return false;

            // The authority gives the new player a pawn, the way admission does.
            AuthorityPawn = Authority.Entities.CreateEntity();
            Authority.Entities.AddComponent<NetReplicated>(AuthorityPawn);
            Authority.Entities.AddComponent<LocalTransform>(
                AuthorityPawn,
                LocalTransform{ Transform3f{ Vec3d(0.0f, 2.0f, 0.0f),
                                             Quatf::Identity(), Vec3d::One() } });
            Authority.Entities.AddComponent<NetOwner>(
                AuthorityPawn,
                NetOwner{ .Peer = Client.Session.LocalPeerId().Value });
            Authority.Entities.AddComponent<InputActionSourceRef>(
                AuthorityPawn,
                InputActionSourceRef{
                    .Source = Client.Session.LocalPeerId().Value });
            Authority.AddBody(AuthorityPawn);
            return true;
        }

        // One wall-clock step: one authority tick and one client tick, in the
        // order the frame phases run them.
        void Advance(const ScriptTick& input)
        {
            Now += kTickSeconds;
            ++AuthorityTick;
            ++ClientTick;
            Host.Session.SetLocalTick(AuthorityTick);
            Client.Session.SetLocalTick(ClientTick);

            //--- Authority frame -------------------------------------------
            for (const NetSession::Delivery& delivery : Host.Session.Pump(Now))
            {
                if (!delivery.Payload.empty()
                    && static_cast<NetPayloadKind>(delivery.Payload[0])
                           == NetPayloadKind::Command)
                {
                    (void)HostCommands.Receive(delivery.From, delivery.Payload);
                }
            }

            // Fixed tick: peer input into its source, then the game derives
            // intent from that source, then movement runs.
            HostCommands.Feed(Authority.Entities, AuthorityTick);
            if (AuthorityPawn.IsValid())
            {
                const InputActionSources sources(Authority.Entities);
                const LookOrientation* look =
                    Authority.Entities.TryGet<LookOrientation>(AuthorityPawn);
                *Authority.Entities.TryGet<MovementIntent>(AuthorityPawn) =
                    DeriveIntentFromActions(sources.TickFor(AuthorityPawn),
                                            look == nullptr ? 0.0f : look->Yaw);
            }
            Authority.TickMovement();

            // Flush: the snapshot leaves in the frame that produced it.
            (void)HostReplication.Publish(Host.Session, Authority.Entities,
                                          Authority.Layout, AuthorityTick,
                                          &HostCommands);
            Host.Session.Flush(Now);

            //--- Client frame ----------------------------------------------
            for (const NetSession::Delivery& delivery : Client.Session.Pump(Now))
            {
                if (delivery.Payload.empty())
                    continue;
                if (static_cast<NetPayloadKind>(delivery.Payload[0])
                    != NetPayloadKind::Snapshot)
                {
                    continue;
                }

                const SnapshotApplyResult applied = ClientReplication.Apply(
                    delivery.Payload, Mirror.Entities, Mirror.Schema,
                    Mirror.Layout, nullptr, &Prediction, nullptr);
                if (!applied.Ok())
                    continue;

                // Every snapshot is a clock sample, the freshest available.
                Clock.Observe(applied.Tick, ClientTick,
                              Client.Session.RoundTripMicroseconds(),
                              kTickSeconds);

                AdoptPawn();

                if (applied.PredictedStateUpdated && ClientPawn.IsValid())
                {
                    PawnReplayRequest replay;
                    replay.Entities = &Mirror.Entities;
                    replay.Schema = &Mirror.Schema;
                    replay.Prediction = &Prediction;
                    replay.Movers = &Mirror.Movers;
                    replay.AckTick = applied.CommandAck;
                    replay.FixedDeltaSeconds = kDt;
                    replay.Gravity = kGravity;
                    replay.UpAxis = kUpAxis;
                    replay.Replay = ReplayEnabled;
                    (void)ReplayPawnState(replay);
                }
            }

            // Fixed tick: the client simulates its own pawn from its own input,
            // immediately -- which is the entire point of prediction.
            if (ClientPawn.IsValid())
            {
                if (LookOrientation* look =
                        Mirror.Entities.TryGet<LookOrientation>(ClientPawn))
                {
                    look->Yaw = input.Yaw;
                }
                *Mirror.Entities.TryGet<MovementIntent>(ClientPawn) =
                    DeriveIntent(input);
            }
            Mirror.TickMovement();

            // Capture the tick, once, where everything downstream reads it.
            if (ClientPawn.IsValid() && Clock.HasEstimate())
            {
                PawnCommandTick record;
                record.Tick = Clock.CommandTickAt(ClientTick);
                record.Yaw = input.Yaw;
                record.Pitch = 0.0f;
                record.Intent = DeriveIntent(input);
                record.ActionCount = kActionCount;
                record.Actions[InputActionRegistry::IndexOf(kMove)].X = input.Strafe;
                record.Actions[InputActionRegistry::IndexOf(kMove)].Y = input.Forward;
                if (input.Jump)
                {
                    record.Actions[InputActionRegistry::IndexOf(kJump)].Flags =
                        InputActionFlags::Pressed | InputActionFlags::Fired;
                }
                Prediction.Commands().Push(record);
            }

            (void)ClientCommands.SendLocal(Client.Session, Prediction.Commands(),
                                           ClientReplication.AppliedSnapshot());
            Client.Session.Flush(Now);
        }

        void AdoptPawn()
        {
            if (ClientPawn.IsValid())
                return;
            for (const auto& [netId, entity] :
                 ClientReplication.ClientEntities().All())
            {
                ClientPawn = entity;
                Mirror.AddBody(ClientPawn);
                Prediction.SetPredicted(ClientPawn);
                break;
            }
        }

        [[nodiscard]] float Disagreement() const
        {
            if (!AuthorityPawn.IsValid() || !ClientPawn.IsValid())
                return 0.0f;
            return (Authority.PositionOf(AuthorityPawn)
                    - Mirror.PositionOf(ClientPawn))
                .Magnitude();
        }
    };

    NetImpairment BadConnection()
    {
        NetImpairment impairment;
        impairment.LossPercent = 10;
        impairment.ReorderPercent = 3;
        impairment.LatencySteps = 4;
        impairment.JitterSteps = 2;
        impairment.Seed = 20260808;
        return impairment;
    }
}

// The incident. Press into the wall through loss, delay, and jitter; keep
// pressing; turn along it; stop. On the old code the two machines drifted
// without bound because nothing that reached the mover, nothing that carried
// velocity, and nothing that shared a tick name ever pulled them together.
TEST(PredictionConvergence, AClientDrivenIntoGeometryConvergesUnderLoss)
{
    ConvergenceRig rig(BadConnection());
    ASSERT_TRUE(rig.Join(600)) << "the session never formed";

    // Settle onto the floor and let the clock name itself.
    for (int step = 0; step < 120; ++step)
        rig.Advance(ScriptTick{});
    ASSERT_TRUE(rig.ClientPawn.IsValid()) << "the pawn was never adopted";

    // Six seconds of exactly what broke it live: full forward into the wall,
    // then angling along it while still pressed against it.
    for (int step = 0; step < 360; ++step)
    {
        ScriptTick input;
        input.Forward = 1.0f;
        input.Yaw = step < 180 ? 0.0f : 0.3f + 0.001f * static_cast<float>(step);
        rig.Advance(input);
    }

    // Stop, and give the link time to deliver the last word both ways.
    for (int step = 0; step < 240; ++step)
        rig.Advance(ScriptTick{});

    EXPECT_LT(rig.Disagreement(), 1e-3f)
        << "the two machines settled " << rig.Disagreement()
        << " metres apart: the loop that pulls them together is not closing";

    EXPECT_GT(rig.Prediction.Reconciles(), 100u)
        << "reconciliation barely ran, so agreement here is coincidence";
    EXPECT_EQ(rig.Prediction.Snaps(), 0u)
        << "the pawn was moved outright on a link that never stalled past the "
           "ring, so the replay path was not actually exercised";

    // The impairment has to have happened, or this is the happy path again.
    EXPECT_GT(rig.Client.Link.Dropped(), 0u);
    EXPECT_GT(rig.Host.Link.Dropped(), 0u);
    EXPECT_GT(rig.Client.Link.Delayed(), 0u);
}

// One press, one jump -- through reconciles that re-run the ticks around it.
// The cooldown rides in the replayed state precisely so a re-run tick reaches
// the same verdict the live tick did, instead of jumping again or not at all.
TEST(PredictionConvergence, AJumpThroughReconcilesIsExactlyOneJump)
{
    ConvergenceRig rig(BadConnection());
    ASSERT_TRUE(rig.Join(600));

    for (int step = 0; step < 120; ++step)
        rig.Advance(ScriptTick{});
    ASSERT_TRUE(rig.ClientPawn.IsValid());

    // Resting height, so an excursion above it is unambiguous.
    const float restY =
        static_cast<float>(rig.Mirror.PositionOf(rig.ClientPawn).Y);

    std::vector<float> heights;
    for (int step = 0; step < 300; ++step)
    {
        ScriptTick input;
        input.Forward = 0.6f;
        input.Jump = step == 60;
        rig.Advance(input);
        heights.push_back(
            static_cast<float>(rig.Mirror.PositionOf(rig.ClientPawn).Y));
    }

    // Contiguous airborne stretches, each one a jump the player saw.
    int episodes = 0;
    bool airborne = false;
    for (const float y : heights)
    {
        const bool up = y > restY + 0.3f;
        if (up && !airborne)
            ++episodes;
        airborne = up;
    }

    EXPECT_EQ(episodes, 1)
        << episodes << " airborne stretches from one press: zero is a jump "
           "eaten by reconciliation, more than one is a replayed tick jumping "
           "again";
}

// Prediction off is the same loop minus the re-running: the pawn lands where
// the authority put it, a round trip late. It has to converge too -- this mode
// was previously broken by the same dead gate, with the authority's word landing
// on the transform and the mover sweeping it away every tick.
TEST(PredictionConvergence, PredictionOffStillConverges)
{
    ConvergenceRig rig(BadConnection());
    rig.ReplayEnabled = false;
    ASSERT_TRUE(rig.Join(600));

    for (int step = 0; step < 120; ++step)
        rig.Advance(ScriptTick{});
    ASSERT_TRUE(rig.ClientPawn.IsValid());

    for (int step = 0; step < 240; ++step)
    {
        ScriptTick input;
        input.Forward = 1.0f;
        rig.Advance(input);
    }
    for (int step = 0; step < 240; ++step)
        rig.Advance(ScriptTick{});

    EXPECT_LT(rig.Disagreement(), 1e-3f)
        << "input-delay mode settled " << rig.Disagreement()
        << " metres apart, so the restore is not actually reaching the mover";
}
