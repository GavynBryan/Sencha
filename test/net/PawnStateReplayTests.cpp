#include <gtest/gtest.h>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/JumpState.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponentTraits.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <net/ClientPrediction.h>
#include <prediction/PawnStateReplay.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <cstring>
#include <vector>

//=============================================================================
// The claim the whole rebuild rests on: replaying a tick reaches the same place
// the live tick reached.
//
// If it does not, reconciliation is a second implementation of movement that
// happens to run on the same inputs, and every reconcile injects the difference
// between the two into the pawn -- which is the failure it was built to remove,
// arriving by a different road. So this drives the real systems over a real
// world with real geometry, keeps the records that produced the result, and
// then demands that replaying them from the same start produces it again.
//
// Exactly. Same code, same order, same machine: anything less than equality
// means the two paths have diverged somewhere and the difference is a number
// nobody chose.
//=============================================================================

namespace
{
    // What a game would reasonably declare Predicted and what no character tick
    // has ever heard of: a cooldown its owner counts down locally so that using
    // an ability feels immediate. Here so the contract can be tested rather
    // than only described.
    struct AbilityCooldown
    {
        float Remaining = 0.0f;
    };
}

template <>
struct TypeSchema<AbilityCooldown>
{
    static constexpr std::string_view Name = "AbilityCooldown";
    static constexpr bool Replicated = true;
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("remaining", &AbilityCooldown::Remaining).OwnerOnly(),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(AbilityCooldown, "test.ability_cooldown");

namespace
{
    constexpr float kDt = 1.0f / 60.0f;
    const Vec3d kGravity{ 0.0f, -9.81f, 0.0f };
    const Vec3d kUpAxis{ 0.0f, 1.0f, 0.0f };

    const StoragePartitionSet& ActivePartitions()
    {
        static const StoragePartitionSet partitions = [] {
            StoragePartitionSet value;
            value.Add(StoragePartitionId::Default());
            return value;
        }();
        return partitions;
    }

    // A floor to stand on and a wall to be stopped by. The wall is the part
    // that matters: an approximation of movement agrees with the real thing
    // right up until something refuses to let the pawn through.
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

    // One machine: a world, its physics, and the movement systems in the order
    // the schedule runs them.
    struct Machine
    {
        WorldComponentSchema Schema;
        // The same table the runtime compiles the predicted set from, so these
        // shadow whatever the engine's components say they resume from.
        ReplicationLayout Layout;
        PhysicsWorld Physics;
        World Entities;
        CharacterMoverPool Movers{ Physics };

        FreeLocomotionSystem Locomotion{ kGravity, kUpAxis };
        JumpExecutionSystem Jump;
        MotionCompositionSystem Composition;
        EntityId Pawn;

        Machine()
        {
            ComponentRegistrar registrar(&Schema, nullptr, &Layout);
            RegisterEngineComponents(registrar);
            // Declared on every machine here, carried by a pawn only where a
            // test is about it. The declaration alone is what the report reads.
            registrar.Add<AbilityCooldown>();
            Schema.Seal();
            Layout.Seal();
            Schema.Apply(Entities);
            RegisterMovement(Entities);
            BuildLevel(Physics);

            Pawn = Entities.CreateEntity();
            Entities.AddComponent<LocalTransform>(
                Pawn, LocalTransform{ Transform3f{ Vec3d(0.0f, 2.0f, 0.0f),
                                                   Quatf::Identity(),
                                                   Vec3d::One() } });
            Entities.AddComponent<CharacterController>(Pawn, CharacterController{});
            // The per-tick scratch arrives with the component that owes it.
            Entities.AddComponent<CharacterMovement>(
                Pawn,
                CharacterMovement{
                    .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode(),
                });
            Movers.Reconcile(Entities, ActivePartitions());
        }

        // One fixed tick through the scheduled systems, exactly as the frame
        // runs them.
        void Tick(const MovementIntent& intent)
        {
            *Entities.TryGet<MovementIntent>(Pawn) = intent;
            Locomotion.Step(Entities, kDt);
            Jump.Step(Entities, kDt);
            Composition.Step(Entities);
            Movers.Reconcile(Entities, ActivePartitions());
            Movers.Drive(Entities, ActivePartitions(), kDt, kGravity);
        }

        [[nodiscard]] Vec3d Position() const
        {
            return Entities.TryGet<LocalTransform>(Pawn)->Value.Position;
        }
    };

    // What a player does over a few seconds: settle, run at the wall, turn
    // along it, jump, keep going. Enough that an approximation would drift.
    std::vector<PawnCommandTick> ScriptedRun(std::uint64_t firstTick,
                                             std::size_t count)
    {
        std::vector<PawnCommandTick> script;
        script.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            PawnCommandTick record;
            record.Tick = firstTick + index;
            const float t = static_cast<float>(index);
            // Running into the wall, then angling along it.
            record.Intent.WishDir =
                Vec3d(1.0f, 0.0f, index < 30 ? 0.0f : 0.6f);
            record.Yaw = t * 0.01f;
            record.Intent.Jump = (index % 37) == 20;
            script.push_back(record);
        }
        return script;
    }

    // Copies one machine's pawn state into a prediction's shadow, the way a
    // snapshot carrying that state would: every component the table calls
    // predicted, so this cannot fall behind the set the runtime uses.
    void ShadowPawn(ClientPrediction& prediction, const Machine& machine,
                    EntityId pawn)
    {
        const World& world = machine.Entities;
        for (const ReplicatedComponent& component : machine.Layout.Components())
        {
            if (!component.Predicted)
                continue;

            const ComponentId column = world.GetComponentIdByType(component.Type);
            if (!world.HasComponent(pawn, column))
                continue;

            const std::span<std::byte> bytes =
                prediction.AuthoritativeBytes(component.Type);
            ASSERT_EQ(bytes.size(), component.Size);
            std::memcpy(bytes.data(), world.GetComponentRaw(pawn, column),
                        component.Size);
            prediction.MarkSeen(component.Type);
        }
    }

    // A prediction bound to a machine's table and pointed at its pawn.
    ClientPrediction PredictingPawn(const Machine& machine)
    {
        ClientPrediction prediction;
        prediction.Bind(machine.Layout);
        prediction.SetPredicted(machine.Pawn);
        return prediction;
    }

    PawnReplayRequest RequestFor(Machine& machine, ClientPrediction& prediction,
                                 std::uint64_t ack)
    {
        PawnReplayRequest request;
        request.Entities = &machine.Entities;
        request.Schema = &machine.Schema;
        request.Prediction = &prediction;
        request.Movers = &machine.Movers;
        request.AckTick = ack;
        request.FixedDeltaSeconds = kDt;
        request.Gravity = kGravity;
        request.UpAxis = kUpAxis;
        return request;
    }
}

TEST(PawnStateReplay, ReachesExactlyWhereTheLiveTicksReached)
{
    constexpr std::uint64_t kFirstTick = 1000;
    // Inside the window this machine keeps: a longer run is a gap, which
    // has its own test below.
    constexpr std::size_t kTicks = PawnCommandRing::kDepth - 4;
    const std::vector<PawnCommandTick> script = ScriptedRun(kFirstTick, kTicks);

    // Run it live, remembering the state it started from and where it ended.
    Machine live;
    for (const PawnCommandTick& record : script)
        live.Tick(record.Intent);
    const Vec3d lived = live.Position();

    // Replay the same records from the same start on a machine that has done
    // nothing else.
    Machine replayed;
    ClientPrediction prediction = PredictingPawn(replayed);
    ShadowPawn(prediction, replayed, replayed.Pawn);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    const PawnReplayResult result =
        ReplayPawnState(RequestFor(replayed, prediction, kFirstTick - 1));

    ASSERT_TRUE(result.Ran);
    EXPECT_FALSE(result.Snapped);
    EXPECT_EQ(result.TicksReplayed, kTicks);

    const Vec3d after = replayed.Position();
    EXPECT_EQ(after.X, lived.X) << "replay and the live tick disagree along X";
    EXPECT_EQ(after.Y, lived.Y) << "replay and the live tick disagree along Y";
    EXPECT_EQ(after.Z, lived.Z) << "replay and the live tick disagree along Z";
}

// The control for the test above: if the two paths were agreeing for some
// reason other than running the same movement over the same input, changing the
// input would not change the answer.
TEST(PawnStateReplay, DisagreesWhenTheInputDisagrees)
{
    constexpr std::uint64_t kFirstTick = 1000;
    std::vector<PawnCommandTick> script = ScriptedRun(kFirstTick, 40);

    Machine live;
    for (const PawnCommandTick& record : script)
        live.Tick(record.Intent);
    const Vec3d lived = live.Position();

    Machine replayed;
    ClientPrediction prediction = PredictingPawn(replayed);
    ShadowPawn(prediction, replayed, replayed.Pawn);
    // Along the wall rather than into it: the wall clamps X in both runs
    // whatever the input said, so a difference there would be invisible.
    script[35].Intent.WishDir = Vec3d(1.0f, 0.0f, -0.6f);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    (void)ReplayPawnState(RequestFor(replayed, prediction, kFirstTick - 1));
    EXPECT_NE(replayed.Position().Z, lived.Z)
        << "one tick of different input produced the same result, so this test "
           "cannot tell the two paths apart";
}

TEST(PawnStateReplay, ReplaysOnlyWhatTheAuthorityHasNotAnswered)
{
    constexpr std::uint64_t kFirstTick = 500;
    const std::vector<PawnCommandTick> script = ScriptedRun(kFirstTick, 20);

    Machine machine;
    ClientPrediction prediction = PredictingPawn(machine);
    ShadowPawn(prediction, machine, machine.Pawn);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    // The authority is done with the first fifteen.
    const PawnReplayResult result =
        ReplayPawnState(RequestFor(machine, prediction, kFirstTick + 14));

    EXPECT_EQ(result.TicksReplayed, 5u)
        << "re-running ticks the authority has already applied would apply the "
           "same input twice";
    EXPECT_EQ(prediction.Commands().Size(), 5u)
        << "answered ticks have to leave the ring, or it fills with history";
}

// The honest failure. A stall longer than the window this machine keeps means
// some unanswered tick has already been forgotten, so replaying what is left
// would skip input the authority is about to apply. Moving the pawn is visible;
// quietly continuing is not.
TEST(PawnStateReplay, SnapsRatherThanReplayingAcrossAGapItCannotFill)
{
    Machine machine;
    ClientPrediction prediction = PredictingPawn(machine);
    ShadowPawn(prediction, machine, machine.Pawn);

    const std::vector<PawnCommandTick> script =
        ScriptedRun(1, PawnCommandRing::kDepth + 30);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    // Acknowledging a tick the ring has long since overwritten.
    const PawnReplayResult result = ReplayPawnState(RequestFor(machine, prediction, 3));

    EXPECT_TRUE(result.Snapped);
    EXPECT_EQ(result.TicksReplayed, 0u);
    EXPECT_EQ(prediction.Commands().Size(), 0u)
        << "records kept after a snap would be replayed onto a state they no "
           "longer follow from";
}

// Off is not a second code path: the same restore runs, and only the re-running
// is skipped. That is the input-delay behaviour, and it has to actually put the
// pawn where the authority says rather than leaving it where it guessed.
TEST(PawnStateReplay, WithReplayOffThePawnLandsOnTheAuthoritysState)
{
    Machine machine;
    ClientPrediction prediction = PredictingPawn(machine);

    LocalTransform authoritative;
    authoritative.Value.Position = Vec3d(3.0f, 2.0f, -1.0f);
    const ComponentTypeId type = ResolveComponentTypeId<LocalTransform>();
    const std::span<std::byte> bytes = prediction.AuthoritativeBytes(type);
    std::memcpy(bytes.data(), &authoritative, sizeof(authoritative));
    prediction.MarkSeen(type);

    const std::vector<PawnCommandTick> script = ScriptedRun(10, 5);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    PawnReplayRequest request = RequestFor(machine, prediction, 9);
    request.Replay = false;
    const PawnReplayResult result = ReplayPawnState(request);

    ASSERT_TRUE(result.Ran);
    EXPECT_EQ(result.TicksReplayed, 0u);
    EXPECT_FLOAT_EQ(machine.Position().X, 3.0f);
    EXPECT_FLOAT_EQ(machine.Position().Z, -1.0f);

    // And through the mover, so the next sweep starts from there rather than
    // from where the mover still believed it was.
    machine.Tick(MovementIntent{});
    EXPECT_NEAR(machine.Position().X, 3.0f, 0.05f)
        << "the restore reached the transform but not the mover, so the first "
           "sweep after it undid the whole thing";
}

// The other honest failure: rules this machine cannot re-run. Replaying under
// the wrong ones would be a disagreement nobody sees, so the pawn is moved
// instead and the count says so.
TEST(PawnStateReplay, SnapsRatherThanReplayingUnderRulesItDoesNotImplement)
{
    Machine machine;
    ClientPrediction prediction = PredictingPawn(machine);

    // A mode this build has no kernel for, arriving as the authority's word.
    CharacterMovement unmodelled;
    unmodelled.Mode = LocomotionModeId{ 9999 };
    const ComponentTypeId type = ResolveComponentTypeId<CharacterMovement>();
    const std::span<std::byte> bytes = prediction.AuthoritativeBytes(type);
    std::memcpy(bytes.data(), &unmodelled, sizeof(unmodelled));
    prediction.MarkSeen(type);

    const std::vector<PawnCommandTick> script = ScriptedRun(100, 6);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    const PawnReplayResult result =
        ReplayPawnState(RequestFor(machine, prediction, 99));

    ASSERT_TRUE(result.Ran);
    EXPECT_TRUE(result.Snapped);
    EXPECT_EQ(result.TicksReplayed, 0u);
    EXPECT_EQ(prediction.Commands().Size(), 0u);
}

// The gate is asked before there is anything to run, because a pawn under rules
// this cannot re-run is out of step whether or not it owes ticks. Deciding
// inside the loop would let an empty ring pass for agreement.
TEST(PawnStateReplay, SnapsUnderRulesItCannotRunEvenWithNothingToReplay)
{
    Machine machine;
    ClientPrediction prediction = PredictingPawn(machine);

    CharacterMovement unmodelled;
    unmodelled.Mode = LocomotionModeId{ 9999 };
    const ComponentTypeId type = ResolveComponentTypeId<CharacterMovement>();
    const std::span<std::byte> bytes = prediction.AuthoritativeBytes(type);
    std::memcpy(bytes.data(), &unmodelled, sizeof(unmodelled));
    prediction.MarkSeen(type);

    const PawnReplayResult result =
        ReplayPawnState(RequestFor(machine, prediction, 500));

    ASSERT_TRUE(result.Ran);
    EXPECT_TRUE(result.Snapped);
    EXPECT_EQ(result.TicksReplayed, 0u);
}

// A driven subject a character tick was never going to move -- a fixed gun,
// which has an aim and a trigger and no way to walk. Nothing about it can have
// diverged, so the authority's word is the whole answer.
//
// It must not be snapped, and the ring is where that shows. A snap clears every
// unanswered command; for a subject whose input is entirely actions those
// records are the only copy of a trigger pull, so the window that exists to
// survive a lost datagram would be emptied on every snapshot that arrived.
TEST(PawnStateReplay, ASubjectThatIsNotACharacterKeepsItsUnansweredCommands)
{
    Machine machine;

    // Everything a driven thing needs except the movement that would make it a
    // character: somewhere to stand, and nothing to walk with.
    const EntityId mount = machine.Entities.CreateEntity();
    machine.Entities.AddComponent<LocalTransform>(
        mount, LocalTransform{ Transform3f{ Vec3d(3.0f, 2.0f, 0.0f),
                                            Quatf::Identity(), Vec3d::One() } });

    ClientPrediction prediction;
    prediction.Bind(machine.Layout);
    prediction.SetPredicted(mount);

    const LocalTransform authority{ Transform3f{ Vec3d(3.0f, 2.0f, 0.0f),
                                                 Quatf::Identity(),
                                                 Vec3d::One() } };
    const ComponentTypeId type = ResolveComponentTypeId<LocalTransform>();
    const std::span<std::byte> bytes = prediction.AuthoritativeBytes(type);
    ASSERT_EQ(bytes.size(), sizeof(authority));
    std::memcpy(bytes.data(), &authority, sizeof(authority));
    prediction.MarkSeen(type);

    const std::vector<PawnCommandTick> script = ScriptedRun(100, 6);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    const PawnReplayResult result =
        ReplayPawnState(RequestFor(machine, prediction, 99));

    ASSERT_TRUE(result.Ran);
    EXPECT_FALSE(result.Snapped)
        << "a subject no character tick would have moved cannot have diverged";
    EXPECT_EQ(result.TicksReplayed, 0u);
    EXPECT_EQ(prediction.Commands().Size(), script.size())
        << "the unanswered window was emptied; a lost datagram now loses input";
}

//=============================================================================
// What Predicted does not buy
//
// The declaration stages a component's arriving bytes apart from the world's
// copy and restores them before the unanswered ticks are re-run. It does not
// arrange for anything to re-run, and the only thing that does is the character
// tick. The two tests below are the contract: the build says which declarations
// fall outside it, and what falling outside it actually costs.
//=============================================================================

TEST(PredictedComponentContract, TheEnginesOwnPredictedSetIsEntirelyResumed)
{
    WorldComponentSchema schema;
    ReplicationLayout layout;
    ComponentRegistrar registrar(&schema, nullptr, &layout);
    RegisterEngineComponents(registrar);
    layout.Seal();

    std::vector<std::string_view> unresumed;
    CollectUnresumedPredictedComponents(layout, unresumed);
    EXPECT_TRUE(unresumed.empty())
        << "an engine component is declared Predicted that a replayed tick does "
           "not resume, so the startup warning fires on a stock build: "
        << (unresumed.empty() ? std::string_view{} : unresumed.front());
}

TEST(PredictedComponentContract, ADeclarationTheReplayCannotResumeIsNamed)
{
    Machine machine;

    std::vector<std::string_view> unresumed;
    CollectUnresumedPredictedComponents(machine.Layout, unresumed);

    ASSERT_EQ(unresumed.size(), 1u);
    // The registered component name rather than the schema's, because that is
    // what net_components and every other net diagnostic calls it.
    EXPECT_EQ(unresumed.front(), "test.ability_cooldown");
}

// The behaviour the name is warning about, asserted deliberately rather than
// left implied. If someone makes a replay able to carry a component like this
// forward, this test fails and points at the contract that needs rewriting --
// which is the whole reason it asserts the unhappy answer.
TEST(PredictedComponentContract, AnUnresumedPredictedComponentIsResetAndNotCarriedForward)
{
    constexpr std::uint64_t kFirstTick = 700;
    constexpr float kAuthoritysWord = 5.0f;
    // Where the owner's own countdown had got to by the time the correction
    // arrived: several ticks below what the authority last said.
    constexpr float kLocallyAdvanced = 3.0f;

    Machine machine;
    machine.Entities.AddComponent<AbilityCooldown>(
        machine.Pawn, AbilityCooldown{ .Remaining = kAuthoritysWord });

    ClientPrediction prediction = PredictingPawn(machine);
    ShadowPawn(prediction, machine, machine.Pawn);

    machine.Entities.TryGet<AbilityCooldown>(machine.Pawn)->Remaining =
        kLocallyAdvanced;

    const std::vector<PawnCommandTick> script = ScriptedRun(kFirstTick, 10);
    for (const PawnCommandTick& record : script)
        prediction.Commands().Push(record);

    const PawnReplayResult result =
        ReplayPawnState(RequestFor(machine, prediction, kFirstTick - 1));

    // The replay really ran, so what follows is not "nothing happened".
    ASSERT_TRUE(result.Ran);
    ASSERT_FALSE(result.Snapped);
    ASSERT_EQ(result.TicksReplayed, 10u);

    const float after =
        machine.Entities.TryGet<AbilityCooldown>(machine.Pawn)->Remaining;
    EXPECT_FLOAT_EQ(after, kAuthoritysWord)
        << "the restore did not happen, which would mean Predicted stages "
           "nothing at all";
    EXPECT_NE(after, kLocallyAdvanced)
        << "the owner's own progress survived a reconcile";
    // The point: ten ticks were re-run and none of them touched this. The
    // countdown is back where the authority left it and no closer to done.
    EXPECT_GT(after, kLocallyAdvanced)
        << "something advanced it during replay, so the contract this test "
           "pins has changed and the header describing it is now wrong";
}
