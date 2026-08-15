#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/ClientPrediction.h>
#include <app/EngineSchedule.h>
#include <net/NetOwnership.h>
#include <net/NetPeerInputSource.h>
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

// Ownership says who a thing belongs to. Participant control says who is at
// its controls, and keeping them apart is what leaves a vehicle standing when
// the peer driving it disconnects.
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
TEST(NetPeerInputSource, APeerLeavingClosesTheSourceItsCommandsLandedIn)
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

    NetReleasePeerSource(fixture.Entities, PeerId{ 7 });

    EXPECT_EQ(sources.Find(source), nullptr)
        << "the peer left and its input slot did not";
}

// A source id is not a peer id. They were the same number while peers were the
// only thing producing input, which left nothing for a source with no peer
// behind it -- a bot, a script, a cutscene driving an actor.
TEST(NetPeerInputSource, APeersSourceIsAllocatedRatherThanItsOwnNumber)
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
TEST(NetPeerInputSource, APeerNumberUsedAgainDoesNotInheritTheOldSource)
{
    OwnershipWorld fixture;
    const InputActionSourceId first =
        NetSourceForPeer(fixture.Entities, PeerId{ 2 });
    ASSERT_NE(first, kLocalInputActionSource);

    NetReleasePeerSource(fixture.Entities, PeerId{ 2 });
    EXPECT_EQ(NetFindSourceForPeer(fixture.Entities, PeerId{ 2 }),
              kLocalInputActionSource)
        << "the departed peer's mapping outlived it";

    (void)NetSourceForPeer(fixture.Entities, PeerId{ 2 });
    EXPECT_NE(NetFindSourceForPeer(fixture.Entities, PeerId{ 2 }), first)
        << "a new holder of the number inherited the old one's input slot";
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
