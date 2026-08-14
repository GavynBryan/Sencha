#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/NetOwnership.h>
#include <net/NetParticipant.h>
#include <net/NetPlayer.h>
#include <net/NetReplicationComponents.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>

//=============================================================================
// The participant lifecycle
//
// The engine runs the sequence and the game answers two questions: what a
// player is made of, and where a body comes from. What used to be a per-frame
// loop over connected peers plus a sweep for pawns nobody owned.
//
// The states worth protecting here are the ones a loop could not express: a
// participant with no body is ordinary rather than pending, and a departure
// costs a body but never the thing the departing player happened to be driving.
//=============================================================================

namespace
{
    struct ParticipantWorld
    {
        WorldComponentSchema Schema;
        World Entities;
        NetParticipantPolicies Policies;

        int Built = 0;
        int Asked = 0;

        ParticipantWorld()
        {
            ComponentRegistrar components(&Schema, nullptr, nullptr);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
        }

        EntityId Thing()
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<LookOrientation>(entity, LookOrientation{});
            return entity;
        }

        // A game that always has somewhere to put a body.
        void ProvideBodies()
        {
            Policies.ProvideBody = [this](World&, EntityId) {
                ++Asked;
                return Thing();
            };
        }

        const NetPlayerControl& ControlOf(EntityId player)
        {
            return *Entities.TryGet<NetPlayerControl>(player);
        }
    };
}

TEST(NetParticipant, AdmissionComposesThePlayerAndBindsItsBody)
{
    ParticipantWorld fixture;
    fixture.Policies.BuildPlayer = [&fixture](World&, EntityId) {
        ++fixture.Built;
    };
    fixture.ProvideBodies();

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 3 }, fixture.Policies);

    ASSERT_TRUE(player.IsValid());
    EXPECT_EQ(fixture.Built, 1) << "the game never got to say what a player is";
    EXPECT_EQ(fixture.Asked, 1);

    const EntityId body = fixture.ControlOf(player).Body;
    ASSERT_TRUE(body.IsValid());
    EXPECT_EQ(fixture.ControlOf(player).ControlSubject, body)
        << "the participant was given a body it is not driving";
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(body))
        << "no other machine can be told the body exists";
    EXPECT_EQ(NetOwnerOf(fixture.Entities, body), PeerId{ 3 });
    EXPECT_EQ(fixture.Entities.TryGet<NetDrivenBy>(body)->Peer, 3u);
}

// A participant with no body is an ordinary state, not a pending one. The
// engine asking again on its own is what would make "waiting for a map" and
// "spectating for good" the same thing.
TEST(NetParticipant, AParticipantWithNoBodyIsAnOrdinaryState)
{
    ParticipantWorld fixture;
    fixture.Policies.ProvideBody = [&fixture](World&, EntityId) {
        ++fixture.Asked;
        return EntityId{};
    };

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 1 }, fixture.Policies);

    ASSERT_TRUE(player.IsValid());
    EXPECT_TRUE(fixture.Entities.IsAlive(player));
    EXPECT_FALSE(fixture.ControlOf(player).Body.IsValid());
    EXPECT_EQ(fixture.Asked, 1) << "the engine asked more than once by itself";
}

// The respawn seam. The game decides when a body is owed again; asking is what
// produces one, and nothing polls on its behalf.
TEST(NetParticipant, AskingAgainLaterProducesABody)
{
    ParticipantWorld fixture;
    bool ready = false;
    fixture.Policies.ProvideBody = [&](World&, EntityId) {
        ++fixture.Asked;
        return ready ? fixture.Thing() : EntityId{};
    };

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 2 }, fixture.Policies);
    ASSERT_FALSE(fixture.ControlOf(player).Body.IsValid());

    ready = true;
    const EntityId body =
        NetRequestPlayerBody(fixture.Entities, player, fixture.Policies);

    ASSERT_TRUE(body.IsValid());
    EXPECT_EQ(fixture.ControlOf(player).Body, body);
    EXPECT_EQ(fixture.Asked, 2);
}

// A second body is not something a caller can mean.
TEST(NetParticipant, AskingForABodyAPlayerAlreadyHasChangesNothing)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();
    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 4 }, fixture.Policies);
    const EntityId body = fixture.ControlOf(player).Body;
    ASSERT_TRUE(body.IsValid());

    EXPECT_EQ(NetRequestPlayerBody(fixture.Entities, player, fixture.Policies),
              body);
    EXPECT_EQ(fixture.Asked, 1) << "the game was asked to build a second body";
    EXPECT_EQ(fixture.ControlOf(player).Body, body);
}

TEST(NetParticipant, DepartureReapsTheBodyByDefault)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();
    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 5 }, fixture.Policies);
    const EntityId body = fixture.ControlOf(player).Body;

    const EntityId reaped =
        NetRetireParticipant(fixture.Entities, player, fixture.Policies);

    EXPECT_EQ(reaped, body) << "the caller cannot report what the departure cost";
    EXPECT_FALSE(fixture.Entities.IsAlive(player));
    EXPECT_FALSE(fixture.Entities.IsAlive(body));
}

// A game that leaves bodies standing says so, rather than working around a reap
// it cannot prevent.
TEST(NetParticipant, APolicyThatDeclinesTheReapLeavesTheBodyStanding)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();
    fixture.Policies.ReapBody = [](World&, EntityId, EntityId) { return false; };

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 6 }, fixture.Policies);
    const EntityId body = fixture.ControlOf(player).Body;

    const EntityId reaped =
        NetRetireParticipant(fixture.Entities, player, fixture.Policies);

    EXPECT_FALSE(reaped.IsValid());
    EXPECT_FALSE(fixture.Entities.IsAlive(player));
    EXPECT_TRUE(fixture.Entities.IsAlive(body))
        << "the policy declined the reap and the body went anyway";
}

// The invariant the whole Body/ControlSubject split exists for: quitting at the
// controls of something costs you your body, never the something.
TEST(NetParticipant, DepartureNeverTakesWhatTheParticipantWasDriving)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();
    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 7 }, fixture.Policies);
    const EntityId body = fixture.ControlOf(player).Body;

    const EntityId jeep = fixture.Thing();
    NetPossess(fixture.Entities, player, jeep);

    NetRetireParticipant(fixture.Entities, player, fixture.Policies);

    EXPECT_FALSE(fixture.Entities.IsAlive(body))
        << "the body was theirs and should have gone with them";
    EXPECT_TRUE(fixture.Entities.IsAlive(jeep))
        << "a driver quitting destroyed the vehicle they were sitting in";
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(jeep), nullptr)
        << "the vehicle still reads a source that will never fill again";
}

// A game that registers nothing still gets participants; they simply have no
// bodies. Nothing here requires a policy to exist.
TEST(NetParticipant, AGameThatRegistersNoPoliciesStillAdmits)
{
    ParticipantWorld fixture;

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{ 8 }, fixture.Policies);

    ASSERT_TRUE(player.IsValid());
    EXPECT_FALSE(fixture.ControlOf(player).Body.IsValid());
    EXPECT_EQ(NetPlayerForPeer(fixture.Entities, PeerId{ 8 }), player);
}

//=============================================================================
// The person at this machine
//
// A local participant goes through the same lifecycle as a peer's. What is
// different is only that the body it is given also takes the look input and the
// camera -- and that is a mark on the player rather than something a caller
// remembers, so it holds on a respawn as well as on a join.
//=============================================================================

TEST(NetParticipant, ALocalParticipantsBodyTakesLocalControl)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Local);

    ASSERT_TRUE(player.IsValid());
    const EntityId body = fixture.ControlOf(player).Body;
    ASSERT_TRUE(body.IsValid());
    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), body)
        << "this machine was given a body and is not driving it";
    EXPECT_TRUE(fixture.Entities.HasComponent<LocalLookControl>(body));
    EXPECT_EQ(NetLocalPlayerOf(fixture.Entities), player);
}

// A host running bots presents none of them. Having no peer is not the same
// question as being the person sitting here.
TEST(NetParticipant, ASimulatedParticipantWithNoPeerDoesNotTakeLocalControl)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();

    const EntityId bot =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Simulated);

    ASSERT_TRUE(bot.IsValid());
    ASSERT_TRUE(fixture.ControlOf(bot).Body.IsValid());
    EXPECT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid())
        << "a bot took the camera off the person at this machine";
    EXPECT_FALSE(NetLocalPlayerOf(fixture.Entities).IsValid());
}

// Two would be two cameras and two sets of look input on one machine. Peerless
// participants all record the authority, so the peer number cannot be what
// tells them apart.
TEST(NetParticipant, AskingForTheLocalParticipantTwiceIsTheSamePerson)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();

    const EntityId first =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Local);
    const EntityId second =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Local);

    EXPECT_EQ(first, second);
    EXPECT_EQ(fixture.Asked, 1) << "the second ask built a second body";
}

// A bot beside the local player is a different participant, not the same one.
TEST(NetParticipant, ABotBesideTheLocalPlayerIsADifferentParticipant)
{
    ParticipantWorld fixture;
    fixture.ProvideBodies();

    const EntityId me =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Local);
    const EntityId bot =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Simulated);

    ASSERT_TRUE(me.IsValid());
    ASSERT_TRUE(bot.IsValid());
    EXPECT_NE(me, bot) << "the bot and the player at this machine are one person";
    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities),
              fixture.ControlOf(me).Body);
}

// The respawn case. A body arriving later takes the camera the same way the
// first one did, because the mark is on the player rather than on the moment.
TEST(NetParticipant, ALocalParticipantTakesUpABodyGivenLater)
{
    ParticipantWorld fixture;
    bool ready = false;
    fixture.Policies.ProvideBody = [&](World&, EntityId) {
        ++fixture.Asked;
        return ready ? fixture.Thing() : EntityId{};
    };

    const EntityId player =
        NetAdmitParticipant(fixture.Entities, PeerId{}, fixture.Policies,
                            NetParticipantPresence::Local);
    ASSERT_FALSE(LocalControlSubjectOf(fixture.Entities).IsValid());

    ready = true;
    const EntityId body =
        NetRequestPlayerBody(fixture.Entities, player, fixture.Policies);

    ASSERT_TRUE(body.IsValid());
    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), body)
        << "a respawned body did not take the camera back";
    EXPECT_TRUE(fixture.Entities.HasComponent<LocalLookControl>(body));
}
