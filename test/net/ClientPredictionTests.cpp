#include <gtest/gtest.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/ClientPrediction.h>
#include <world/RuntimeComponentSchema.h>

#include <cstring>

//=============================================================================
// What a client keeps in order to be put back in step with the authority.
//
// Two things, and the design turns on both being present. The authority's own
// view of the pawn, apart from the world's copy -- which is this machine's
// guess and cannot be used to stage a delta against. And every tick of input
// the authority has not answered yet, so that starting again from its state
// does not throw away what the player has done since.
//=============================================================================

namespace
{
    constexpr EntityId kPawn{ 5, 1 };
    constexpr EntityId kOther{ 6, 1 };

    // Stands in for the snapshot applier: decodes a value into one of the
    // shadow slots the way an arriving delta would.
    template <typename T>
    void Decode(ClientPrediction& prediction, const T& value)
    {
        const ComponentTypeId type = ResolveComponentTypeId<T>();
        const std::span<std::byte> shadow = prediction.AuthoritativeBytes(type);
        std::memcpy(shadow.data(), &value, sizeof(value));
        prediction.MarkSeen(type);
    }

    LocalTransform PoseAt(float x)
    {
        LocalTransform pose;
        pose.Value.Position = Vec3d{ x, 0.0f, 0.0f };
        return pose;
    }
}

TEST(ClientPrediction, PredictsOnlyTheEntityItWasGiven)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);

    EXPECT_TRUE(prediction.Predicts(kPawn));
    EXPECT_FALSE(prediction.Predicts(kOther))
        << "another player's pawn is a puppet and must arrive as state";

    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<LocalTransform>()));
    EXPECT_FALSE(prediction.Intercepts(kOther, ResolveComponentTypeId<LocalTransform>()));
}

// The set is the pawn's movement state, because that is what resuming a
// simulation needs. Position alone puts the pawn in the right place still
// carrying the wrong everything else.
TEST(ClientPrediction, WithholdsEverythingAReplayResumesFrom)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);

    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<LocalTransform>()));
    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<KinematicState>()));
    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<SupportState>()));
    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<CharacterMovement>()));
    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<JumpState>()));

    // Everything else about a predicted entity still lands in the world.
    EXPECT_FALSE(prediction.Intercepts(kPawn, ResolveComponentTypeId<WorldTransform>()));
}

TEST(ClientPrediction, DisabledInterceptsNothing)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);
    prediction.SetEnabled(false);

    EXPECT_FALSE(prediction.Intercepts(kPawn, ResolveComponentTypeId<LocalTransform>()))
        << "off has to let the authority's state reach the world, or the pawn "
           "answers to nothing at all";
}

//-----------------------------------------------------------------------------
// The authority's own view
//-----------------------------------------------------------------------------

TEST(ClientPredictionShadow, RemembersEachComponentSeparately)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);

    EXPECT_FALSE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<KinematicState>()));

    KinematicState motion;
    motion.Velocity = Vec3d{ 2.0f, 0.0f, 0.0f };
    Decode(prediction, motion);

    EXPECT_TRUE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<KinematicState>()));
    EXPECT_FALSE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<SupportState>()))
        << "hearing about one component is not hearing about another, and a "
           "delta staged against a component nobody has described is staged "
           "against nothing";
}

TEST(ClientPredictionShadow, PutsEverythingItHasHeardOntoTheEntity)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    World world;
    schema.Apply(world);
    const EntityId pawn = world.CreateEntity();
    world.AddComponent<LocalTransform>(pawn, LocalTransform{});
    world.AddComponent<KinematicState>(pawn, KinematicState{});
    world.AddComponent<JumpState>(pawn, JumpState{});

    ClientPrediction prediction;
    prediction.SetPredicted(pawn);
    Decode(prediction, PoseAt(9.0f));
    KinematicState motion;
    motion.Velocity = Vec3d{ 0.0f, 5.0f, 0.0f };
    Decode(prediction, motion);
    Decode(prediction, JumpState{ .CooldownRemaining = 0.1f });

    ASSERT_TRUE(prediction.RestoreTo(world, schema, pawn));
    EXPECT_FLOAT_EQ(world.TryGet<LocalTransform>(pawn)->Value.Position.X, 9.0f);
    EXPECT_FLOAT_EQ(world.TryGet<KinematicState>(pawn)->Velocity.Y, 5.0f)
        << "restoring the position without the velocity leaves the pawn in the "
           "right place travelling the wrong way, and the next tick walks it "
           "straight back out";
    EXPECT_FLOAT_EQ(world.TryGet<JumpState>(pawn)->CooldownRemaining, 0.1f);
}

TEST(ClientPredictionShadow, SaysNothingAboutAComponentItHasNotHeardOf)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    World world;
    schema.Apply(world);
    const EntityId pawn = world.CreateEntity();
    world.AddComponent<LocalTransform>(pawn, LocalTransform{});
    KinematicState untouched;
    untouched.Velocity = Vec3d{ 7.0f, 0.0f, 0.0f };
    world.AddComponent<KinematicState>(pawn, untouched);

    ClientPrediction prediction;
    prediction.SetPredicted(pawn);
    Decode(prediction, PoseAt(1.0f));

    ASSERT_TRUE(prediction.RestoreTo(world, schema, pawn));
    EXPECT_FLOAT_EQ(world.TryGet<KinematicState>(pawn)->Velocity.X, 7.0f)
        << "a component the authority has never described was overwritten with "
           "an invented default";
}

//-----------------------------------------------------------------------------
// Lifetime
//-----------------------------------------------------------------------------

TEST(ClientPrediction, ChangingPawnsForgetsTheOldOne)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);
    Decode(prediction, PoseAt(3.0f));

    PawnCommandTick record;
    record.Tick = 40;
    prediction.Commands().Push(record);
    ASSERT_EQ(prediction.Commands().Size(), 1u);

    prediction.SetPredicted(kOther);
    EXPECT_EQ(prediction.Predicted(), kOther);
    EXPECT_EQ(prediction.Commands().Size(), 0u)
        << "input kept for one pawn would be replayed onto another";
    EXPECT_FALSE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<LocalTransform>()));
}

TEST(ClientPrediction, SettingTheSamePawnAgainKeepsWhatItHas)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);
    PawnCommandTick record;
    record.Tick = 12;
    prediction.Commands().Push(record);

    prediction.SetPredicted(kPawn);
    EXPECT_EQ(prediction.Commands().Size(), 1u)
        << "re-adopting the pawn a client already has would throw away every "
           "tick it is still owed an answer for";
}

TEST(ClientPrediction, ResetForgetsEverything)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);
    Decode(prediction, PoseAt(3.0f));
    prediction.NoteReconcile(4, 0.5f, true);

    prediction.Reset();
    EXPECT_FALSE(prediction.Predicted().IsValid());
    EXPECT_FALSE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<LocalTransform>()));
    EXPECT_EQ(prediction.Reconciles(), 0u);
    EXPECT_EQ(prediction.Snaps(), 0u);
    EXPECT_FLOAT_EQ(prediction.LastResetMeters(), 0.0f);
}

TEST(ClientPredictionStats, ReportWhatEachReconcileCost)
{
    ClientPrediction prediction;
    prediction.SetPredicted(kPawn);

    prediction.NoteReconcile(3, 0.02f, false);
    EXPECT_EQ(prediction.Reconciles(), 1u);
    EXPECT_EQ(prediction.LastReplayedTicks(), 3u);
    EXPECT_FLOAT_EQ(prediction.LastResetMeters(), 0.02f);
    EXPECT_EQ(prediction.Snaps(), 0u);

    prediction.NoteReconcile(0, 4.0f, true);
    EXPECT_EQ(prediction.Reconciles(), 2u);
    EXPECT_EQ(prediction.Snaps(), 1u);
}
