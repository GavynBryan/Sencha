#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/ClientPrediction.h>
#include <app/EngineSchedule.h>
#include <net/NetOwnership.h>
#include <net/PeerCommandRuntime.h>
#include <net/NetReplicationComponents.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>

#include <vector>

//=============================================================================
// Ownership, and the facts that follow from it
//
// "Peer P drives entity E" used to be several records that had to agree, kept
// in agreement by whoever remembered to. These cover the agreeing.
//=============================================================================

namespace
{
    struct OwnershipWorld
    {
        WorldComponentSchema Schema;
        World Entities;

        OwnershipWorld()
        {
            ComponentRegistrar components(&Schema, nullptr, nullptr);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
        }

        EntityId Pawn()
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<LookOrientation>(entity, LookOrientation{});
            return entity;
        }
    };

    std::vector<EntityId> OwnedBy(const World& world, PeerId peer)
    {
        std::vector<EntityId> out;
        NetOwnedBy(world, peer, out);
        return out;
    }
}

TEST(NetOwnership, GrantingInstallsEverythingThatFollowsFromIt)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();

    NetSetOwner(fixture.Entities, pawn, PeerId{ 3 });

    const NetOwner* owner = fixture.Entities.TryGet<NetOwner>(pawn);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->Peer, 3u);

    const InputActionSourceRef* source =
        fixture.Entities.TryGet<InputActionSourceRef>(pawn);
    ASSERT_NE(source, nullptr) << "whose aim turns it was set and whose keys move "
                                 "it was not";
    EXPECT_EQ(source->Source, 3u);
    EXPECT_EQ(NetOwnerOf(fixture.Entities, pawn), PeerId{ 3 });
}

TEST(NetOwnership, GrantingTwiceChangesNothing)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });
    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });

    EXPECT_EQ(NetOwnerOf(fixture.Entities, pawn), PeerId{ 2 });
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(pawn)->Source, 2u);
}

// One call, because two would leave a frame in which the old owner's keys and
// the new owner's aim both reach the same entity.
TEST(NetOwnership, TransferMovesEveryDerivedFactAtOnce)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 1 });

    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });

    EXPECT_EQ(NetOwnerOf(fixture.Entities, pawn), PeerId{ 2 });
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(pawn)->Source, 2u)
        << "the new owner's aim turns a pawn the old owner's keys still move";
    EXPECT_TRUE(OwnedBy(fixture.Entities, PeerId{ 1 }).empty());
    EXPECT_EQ(OwnedBy(fixture.Entities, PeerId{ 2 }).size(), 1u);
}

TEST(NetOwnership, RelinquishingHandsItBackAsAValue)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 4 });

    NetClearOwner(fixture.Entities, pawn);

    const NetOwner* owner = fixture.Entities.TryGet<NetOwner>(pawn);
    ASSERT_NE(owner, nullptr)
        << "the component went, and a snapshot cannot say a component is gone";
    EXPECT_EQ(owner->Peer, kNetAuthorityPeer);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(pawn), nullptr)
        << "an entity nobody owns still reads somebody's keys";
    EXPECT_FALSE(NetOwnerOf(fixture.Entities, pawn).IsValid());
}

TEST(NetOwnership, APeerLeavingReleasesEverythingItDrove)
{
    OwnershipWorld fixture;
    const EntityId first = fixture.Pawn();
    const EntityId second = fixture.Pawn();
    const EntityId other = fixture.Pawn();
    NetSetOwner(fixture.Entities, first, PeerId{ 5 });
    NetSetOwner(fixture.Entities, second, PeerId{ 5 });
    NetSetOwner(fixture.Entities, other, PeerId{ 6 });

    NetForgetOwnerPeer(fixture.Entities, PeerId{ 5 });

    EXPECT_TRUE(OwnedBy(fixture.Entities, PeerId{ 5 }).empty());
    EXPECT_EQ(OwnedBy(fixture.Entities, PeerId{ 6 }).size(), 1u)
        << "one peer leaving disowned another peer's entity";
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(first), nullptr);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(second), nullptr);
}

// Nothing else closes one, so every peer that ever connects would otherwise
// leave an action state behind for the life of the process.
TEST(NetOwnership, APeerLeavingClosesTheSourceItsCommandsLandedIn)
{
    OwnershipWorld fixture;
    InputActionSourceTable& sources =
        fixture.Entities.AddResource<InputActionSourceTable>();
    (void)sources.Open(7, 4);
    ASSERT_NE(sources.Find(7), nullptr);

    NetForgetOwnerPeer(fixture.Entities, PeerId{ 7 });

    EXPECT_EQ(sources.Find(7), nullptr) << "the peer left and its input slot did not";
}

//=============================================================================
// Local control
//=============================================================================

TEST(NetLocalControl, TakingControlInstallsThePerMachineFacts)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId pawn = fixture.Pawn();

    NetSetLocalControl(fixture.Entities, pawn, &prediction);

    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), pawn);
    EXPECT_TRUE(fixture.Entities.HasComponent<LocalLookControl>(pawn));
    EXPECT_EQ(prediction.Predicted(), pawn);
}

// The falling edge, which nothing did before: a client that stopped owning its
// pawn went on predicting it and went on turning it with the player's mouse.
TEST(NetLocalControl, RelinquishingTakesThemAllBackOff)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId pawn = fixture.Pawn();
    NetSetLocalControl(fixture.Entities, pawn, &prediction);

    NetSetLocalControl(fixture.Entities, EntityId{}, &prediction);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(pawn));
    EXPECT_FALSE(prediction.Predicted().IsValid())
        << "the client still predicts a pawn it no longer drives";
}

TEST(NetLocalControl, MovingControlLeavesNothingOnTheOldEntity)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId first = fixture.Pawn();
    const EntityId second = fixture.Pawn();

    NetSetLocalControl(fixture.Entities, first, &prediction);
    NetSetLocalControl(fixture.Entities, second, &prediction);

    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(first))
        << "two entities turn with one mouse";
    EXPECT_TRUE(fixture.Entities.HasComponent<LocalLookControl>(second));
    EXPECT_EQ(prediction.Predicted(), second);
}

// An authority drives a pawn and predicts nothing about it, so it asks for one
// without a predictor and nothing dereferences a null.
TEST(NetLocalControl, AMachineThatPredictsNothingStillTakesControl)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();

    NetSetLocalControl(fixture.Entities, pawn, nullptr);

    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), pawn);
    EXPECT_TRUE(fixture.Entities.HasComponent<LocalLookControl>(pawn));
}

TEST(NetLocalControl, ADestroyedSubjectReadsBackAsNone)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId pawn = fixture.Pawn();
    NetSetLocalControl(fixture.Entities, pawn, &prediction);

    fixture.Entities.DestroyEntity(pawn);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
}

//=============================================================================
// Reconciling the two, on a client
//=============================================================================

TEST(NetLocalControlReconcile, AClientTakesUpThePawnTheAuthorityGaveIt)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId mine = fixture.Pawn();
    const EntityId theirs = fixture.Pawn();
    NetSetOwner(fixture.Entities, mine, PeerId{ 2 });
    NetSetOwner(fixture.Entities, theirs, PeerId{ 3 });

    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), mine);
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(theirs));
}

TEST(NetLocalControlReconcile, AClientLetsGoWhenOwnershipMovesAway)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });
    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);
    ASSERT_EQ(LocalControlSubjectOf(fixture.Entities), pawn);

    // The authority gave it to somebody else, and the client is told by the
    // replicated component changing under it.
    NetSetOwner(fixture.Entities, pawn, PeerId{ 9 });
    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(pawn));
    EXPECT_FALSE(prediction.Predicted().IsValid());
}

TEST(NetLocalControlReconcile, AClientOwningNothingDrivesNothing)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId theirs = fixture.Pawn();
    NetSetOwner(fixture.Entities, theirs, PeerId{ 8 });

    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
}

// Owning two is not a supported shape. Resolving it the same way every time is
// what makes that a reportable bug rather than an intermittent one -- the scan
// this replaced walked an unordered_map and its winner changed between frames.
TEST(NetLocalControlReconcile, TwoOwnedEntitiesResolveTheSameWayEveryTime)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId first = fixture.Pawn();
    const EntityId second = fixture.Pawn();
    NetSetOwner(fixture.Entities, first, PeerId{ 2 });
    NetSetOwner(fixture.Entities, second, PeerId{ 2 });

    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);
    const EntityId chosen = LocalControlSubjectOf(fixture.Entities);

    for (int repeat = 0; repeat < 8; ++repeat)
    {
        NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);
        EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), chosen);
    }
}

//=============================================================================
// Composition
//
// The two edges the net input channel needs are the engine's, and both name a
// system only the game has. Restating them in every game is how the first game
// that does not gets a remote player steering on last tick's input.
//=============================================================================

namespace
{
    // Stands in for a game's own system that turns resolved actions into
    // intent -- the type the engine cannot name.
    struct ActionConsumer
    {
        void FixedLogic(FixedLogicContext&) {}
    };
}

TEST(NetComposition, TheInputChannelIsOrderedAroundTheGamesOwnSystem)
{
    EngineSchedule schedule;
    PeerCommandRuntime commands;
    ClientPrediction prediction;
    ReplicationInterpolation interpolation;
    NetTickEstimator clock;

    RegisterNetSystems(schedule, commands, prediction, interpolation, clock);
    schedule.Register<ActionConsumer>();

    // Both edges land, which the schedule now refuses to pretend about: an
    // edge naming a system that is not registered, or one whose ends share no
    // phase, is an assertion rather than a silent no-op.
    OrderNetInputAround<ActionConsumer>(schedule);
    schedule.Init();

    EXPECT_TRUE(schedule.Has<PeerCommandFeedSystem>());
    EXPECT_TRUE(schedule.Has<PawnCommandCaptureSystem>());
    EXPECT_TRUE(schedule.Has<ActionConsumer>());
}
