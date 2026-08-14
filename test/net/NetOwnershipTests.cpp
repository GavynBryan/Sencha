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

// Ownership says who a thing belongs to. Who is at its controls is NetPossess,
// and keeping them apart is what leaves a vehicle standing when the peer driving
// it disconnects -- see NetPlayerTests.
TEST(NetOwnership, GrantingRecordsWhoItBelongsToAndNotWhoDrivesIt)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();

    NetSetOwner(fixture.Entities, pawn, PeerId{ 3 });

    const NetOwner* owner = fixture.Entities.TryGet<NetOwner>(pawn);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->Peer, 3u);
    EXPECT_EQ(NetOwnerOf(fixture.Entities, pawn), PeerId{ 3 });

    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(pawn), nullptr)
        << "granting ownership put somebody at the controls as well";
    EXPECT_FALSE(fixture.Entities.HasComponent<NetDrivenBy>(pawn))
        << "a thing somebody owns was reported as a thing somebody drives";
}

TEST(NetOwnership, GrantingTwiceChangesNothing)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });
    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });

    EXPECT_EQ(NetOwnerOf(fixture.Entities, pawn), PeerId{ 2 });
    EXPECT_EQ(OwnedBy(fixture.Entities, PeerId{ 2 }).size(), 1u);
}

// One call, because two would leave a frame in which two peers both hold it.
TEST(NetOwnership, TransferMovesOwnershipAtOnce)
{
    OwnershipWorld fixture;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 1 });

    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });

    EXPECT_EQ(NetOwnerOf(fixture.Entities, pawn), PeerId{ 2 });
    EXPECT_TRUE(OwnedBy(fixture.Entities, PeerId{ 1 }).empty())
        << "the old owner still holds what the new one was given";
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
}

// Nothing else closes one, so every peer that ever connects would otherwise
// leave an action state behind for the life of the process.
TEST(NetOwnership, APeerLeavingClosesTheSourceItsCommandsLandedIn)
{
    OwnershipWorld fixture;
    InputActionSourceTable& sources =
        fixture.Entities.AddResource<InputActionSourceTable>();

    // Opened under the id the peer's commands actually land in, which is
    // allocated rather than assumed to be the peer's own number.
    const InputActionSourceId source =
        NetSourceForPeer(fixture.Entities, PeerId{ 7 });
    (void)sources.Open(source, 4);
    ASSERT_NE(sources.Find(source), nullptr);

    NetForgetOwnerPeer(fixture.Entities, PeerId{ 7 });

    EXPECT_EQ(sources.Find(source), nullptr)
        << "the peer left and its input slot did not";
}

// A source id is not a peer id. They were the same number while peers were the
// only thing producing input, which left nothing for a source with no peer
// behind it -- a bot, a script, a cutscene driving an actor.
TEST(NetOwnership, APeersSourceIsAllocatedRatherThanItsOwnNumber)
{
    OwnershipWorld fixture;
    InputActionSourceIds& ids =
        fixture.Entities.AddResource<InputActionSourceIds>();

    // Something with no peer behind it takes an id first, so the peer's cannot
    // silently coincide with its own number.
    const InputActionSourceId bot = ids.Allocate();

    const InputActionSourceId peerSource =
        NetSourceForPeer(fixture.Entities, PeerId{ 1 });

    EXPECT_NE(peerSource, bot) << "a bot and a peer were handed the same source";
    EXPECT_NE(peerSource, kLocalInputActionSource)
        << "a remote peer's commands land in this machine's own action state";
}

// A session hands peer numbers out in order and a new session starts over, so a
// number outlives the peer that held it.
TEST(NetOwnership, APeerNumberUsedAgainDoesNotInheritTheOldSource)
{
    OwnershipWorld fixture;
    const InputActionSourceId first =
        NetSourceForPeer(fixture.Entities, PeerId{ 2 });
    ASSERT_NE(first, kLocalInputActionSource);

    NetForgetOwnerPeer(fixture.Entities, PeerId{ 2 });
    EXPECT_EQ(NetFindSourceForPeer(fixture.Entities, PeerId{ 2 }),
              kLocalInputActionSource)
        << "the departed peer's mapping outlived it";

    (void)NetSourceForPeer(fixture.Entities, PeerId{ 2 });
    EXPECT_NE(NetFindSourceForPeer(fixture.Entities, PeerId{ 2 }), first)
        << "a new holder of the number inherited the old one's input slot";
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

// A client reconciles against what it is told it DRIVES, not what it is told it
// owns. The two differ for a player whose body is theirs while they are at the
// controls of something else, and asking ownership could not tell which of the
// two to take up. Written directly here because that is what a snapshot does.
TEST(NetLocalControlReconcile, AClientTakesUpWhatTheAuthoritySaysItDrives)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId mine = fixture.Pawn();
    const EntityId theirs = fixture.Pawn();
    fixture.Entities.AddComponent<NetDrivenBy>(mine, NetDrivenBy{ .Peer = 2 });
    fixture.Entities.AddComponent<NetDrivenBy>(theirs, NetDrivenBy{ .Peer = 3 });

    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), mine);
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(theirs));
}

// Owning it is not driving it. A client whose body is still theirs while
// somebody else is at its controls must not take it up.
TEST(NetLocalControlReconcile, OwningSomethingIsNotEnoughToDriveIt)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId pawn = fixture.Pawn();
    NetSetOwner(fixture.Entities, pawn, PeerId{ 2 });

    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
}

TEST(NetLocalControlReconcile, AClientLetsGoWhenControlMovesAway)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId pawn = fixture.Pawn();
    fixture.Entities.AddComponent<NetDrivenBy>(pawn, NetDrivenBy{ .Peer = 2 });
    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);
    ASSERT_EQ(LocalControlSubjectOf(fixture.Entities), pawn);

    // The authority handed it to somebody else, and the client is told by the
    // replicated component changing under it.
    fixture.Entities.TryGet<NetDrivenBy>(pawn)->Peer = 9;
    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(pawn));
    EXPECT_FALSE(prediction.Predicted().IsValid());
}

TEST(NetLocalControlReconcile, AClientDrivingNothingDrivesNothing)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId theirs = fixture.Pawn();
    fixture.Entities.AddComponent<NetDrivenBy>(theirs, NetDrivenBy{ .Peer = 8 });

    NetReconcileLocalControl(fixture.Entities, PeerId{ 2 }, prediction);

    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());
}

// NetPossess gives a player one subject at a time, so two entities naming the
// same driver is a defect rather than a shape. Resolving it the same way every
// time is what makes such a defect reportable instead of intermittent.
TEST(NetLocalControlReconcile, TwoDrivenEntitiesResolveTheSameWayEveryTime)
{
    OwnershipWorld fixture;
    ClientPrediction prediction;
    const EntityId first = fixture.Pawn();
    const EntityId second = fixture.Pawn();
    fixture.Entities.AddComponent<NetDrivenBy>(first, NetDrivenBy{ .Peer = 2 });
    fixture.Entities.AddComponent<NetDrivenBy>(second, NetDrivenBy{ .Peer = 2 });

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

// The shape every caller actually writes: one vector, reused around a loop over
// peers. An appending answer leaves each peer holding everything the peers
// before it owned, which reads as one player driving another's pawn -- and is
// invisible until a session has a second peer in it, which is where a live
// three-process run found it.
TEST(NetOwnership, AskingAboutASecondPeerDoesNotInheritTheFirstsEntities)
{
    OwnershipWorld fixture;
    const EntityId mine = fixture.Pawn();
    const EntityId yours = fixture.Pawn();
    NetSetOwner(fixture.Entities, mine, PeerId{ 1 });
    NetSetOwner(fixture.Entities, yours, PeerId{ 2 });

    std::vector<EntityId> owned;
    NetOwnedBy(fixture.Entities, PeerId{ 1 }, owned);
    ASSERT_EQ(owned.size(), 1u);
    EXPECT_EQ(owned.front(), mine);

    NetOwnedBy(fixture.Entities, PeerId{ 2 }, owned);
    ASSERT_EQ(owned.size(), 1u)
        << "the second peer was credited with the first peer's entities";
    EXPECT_EQ(owned.front(), yours);

    // And a peer that owns nothing answers with nothing rather than with
    // whoever was asked about last.
    NetOwnedBy(fixture.Entities, PeerId{ 3 }, owned);
    EXPECT_TRUE(owned.empty());
}
