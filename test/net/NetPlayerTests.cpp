#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/NetOwnership.h>
#include <net/NetPlayer.h>
#include <net/NetReplicationComponents.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>

//=============================================================================
// The player, and the two things it points at
//
// A participant outlives its body. These cover the difference between what
// belongs to somebody and what they are currently at the controls of -- which
// is the distinction that decides whether a turret survives its driver
// disconnecting, and whether a body waiting outside one is mistaken for
// abandoned.
//=============================================================================

namespace
{
    struct PlayerWorld
    {
        WorldComponentSchema Schema;
        World Entities;

        PlayerWorld()
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

        const NetPlayerControl& ControlOf(EntityId player)
        {
            return *Entities.TryGet<NetPlayerControl>(player);
        }
    };
}

TEST(NetPlayer, AdmissionGivesAParticipantAnIdentityAndASource)
{
    PlayerWorld fixture;

    const EntityId player = NetAdmitPlayer(fixture.Entities, PeerId{ 4 });

    ASSERT_TRUE(player.IsValid());
    EXPECT_EQ(fixture.Entities.TryGet<NetPlayer>(player)->Peer, 4u);
    EXPECT_NE(fixture.ControlOf(player).Source, kLocalInputActionSource)
        << "a remote participant reads the devices on this desk";
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(player))
        << "no other machine can be told this participant exists";
    EXPECT_EQ(NetPlayerForPeer(fixture.Entities, PeerId{ 4 }), player);
}

// The same peer admitted twice is the same person, not a second one.
TEST(NetPlayer, AdmittingAPeerTwiceIsTheSameParticipant)
{
    PlayerWorld fixture;
    const EntityId first = NetAdmitPlayer(fixture.Entities, PeerId{ 1 });

    EXPECT_EQ(NetAdmitPlayer(fixture.Entities, PeerId{ 1 }), first);
}

// Having no peer does not mean reading this machine's devices. Only the person
// sitting here does that; a bot or a script has no peer either, and handing them
// the local source is how a host ends up watching their own keys drive somebody
// else.
TEST(NetPlayer, OnlyTheParticipantAtThisMachineReadsTheLocalSource)
{
    PlayerWorld fixture;

    const EntityId me = NetAdmitPlayer(fixture.Entities, PeerId{},
                                       NetParticipantPresence::Local);
    const EntityId bot = NetAdmitPlayer(fixture.Entities, PeerId{},
                                        NetParticipantPresence::Simulated);

    ASSERT_TRUE(me.IsValid());
    ASSERT_TRUE(bot.IsValid());
    EXPECT_EQ(fixture.ControlOf(me).Source, kLocalInputActionSource);
    EXPECT_NE(fixture.ControlOf(bot).Source, kLocalInputActionSource);

    // Both record the authority, which is why the peer number cannot be what
    // tells them apart.
    EXPECT_EQ(fixture.Entities.TryGet<NetPlayer>(me)->Peer, kNetAuthorityPeer);
    EXPECT_EQ(fixture.Entities.TryGet<NetPlayer>(bot)->Peer, kNetAuthorityPeer);
}

// The whole reason the player exists: dying does not remove somebody from the
// session, so a score, a team, and a respawn timer have somewhere to live.
TEST(NetPlayer, APlayerOutlivesItsBody)
{
    PlayerWorld fixture;
    const EntityId player = NetAdmitPlayer(fixture.Entities, PeerId{ 2 });
    const EntityId body = fixture.Thing();
    fixture.Entities.TryGet<NetPlayerControl>(player)->Body = body;
    NetPossess(fixture.Entities, player, body);

    fixture.Entities.DestroyEntity(body);

    EXPECT_TRUE(fixture.Entities.IsAlive(player))
        << "the participant died with the body it happened to be wearing";
    EXPECT_EQ(NetPlayerForPeer(fixture.Entities, PeerId{ 2 }), player);
}

TEST(NetPlayer, PossessingInstallsTheInputLinkAndTheDriverRecord)
{
    PlayerWorld fixture;
    const EntityId player = NetAdmitPlayer(fixture.Entities, PeerId{ 3 });
    const EntityId thing = fixture.Thing();

    NetPossess(fixture.Entities, player, thing);

    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(thing)->Source,
              fixture.ControlOf(player).Source);
    EXPECT_EQ(fixture.Entities.TryGet<NetDrivenBy>(thing)->Peer, 3u);
    EXPECT_EQ(fixture.ControlOf(player).ControlSubject, thing);

    // Driving is not owning. Nothing here says the thing belongs to them.
    EXPECT_FALSE(NetOwnerOf(fixture.Entities, thing).IsValid())
        << "being at the controls of something made it yours";
}

// Body and subject are separate facts, and the turret is why. The body waiting
// outside is still theirs; only what their keys reach has moved.
TEST(NetPlayer, DrivingSomethingElseLeavesTheBodyWhereItIs)
{
    PlayerWorld fixture;
    const EntityId player = NetAdmitPlayer(fixture.Entities, PeerId{ 5 });
    const EntityId body = fixture.Thing();
    const EntityId turret = fixture.Thing();
    fixture.Entities.TryGet<NetPlayerControl>(player)->Body = body;
    NetPossess(fixture.Entities, player, body);

    NetPossess(fixture.Entities, player, turret);

    EXPECT_EQ(fixture.ControlOf(player).Body, body)
        << "climbing into a turret lost the body that was left outside";
    EXPECT_EQ(fixture.ControlOf(player).ControlSubject, turret);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(body), nullptr)
        << "their keys still reach the body they got out of";
    EXPECT_FALSE(fixture.Entities.HasComponent<NetDrivenBy>(body));
}

// No frame exists in which one player's input reaches two things.
TEST(NetPlayer, TakingSomethingReleasesWhatWasHeldFirst)
{
    PlayerWorld fixture;
    const EntityId player = NetAdmitPlayer(fixture.Entities, PeerId{ 6 });
    const EntityId first = fixture.Thing();
    const EntityId second = fixture.Thing();

    NetPossess(fixture.Entities, player, first);
    NetPossess(fixture.Entities, player, second);

    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(first), nullptr);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(second)->Source,
              fixture.ControlOf(player).Source);
}

// A subject has one driver. The player who loses it must stop recording that
// they have it, or the stale record decides the wrong thing when they retire.
TEST(NetPlayer, TakingSomethingFromAnotherPlayerClearsTheirRecordOfIt)
{
    PlayerWorld fixture;
    const EntityId first = NetAdmitPlayer(fixture.Entities, PeerId{ 7 });
    const EntityId second = NetAdmitPlayer(fixture.Entities, PeerId{ 8 });
    const EntityId turret = fixture.Thing();
    NetPossess(fixture.Entities, first, turret);

    NetPossess(fixture.Entities, second, turret);

    EXPECT_FALSE(fixture.ControlOf(first).ControlSubject.IsValid())
        << "two players both believe they are driving the same thing";
    EXPECT_EQ(fixture.ControlOf(second).ControlSubject, turret);
    EXPECT_EQ(fixture.Entities.TryGet<NetDrivenBy>(turret)->Peer, 8u);
}

// The invariant that decides what a disconnect costs. Somebody quitting at the
// controls of a vehicle does not take the vehicle with them, and the body they
// left is a decision the game makes rather than one retirement makes for it.
TEST(NetPlayer, RetiringReleasesWhatWasDrivenWithoutDestroyingIt)
{
    PlayerWorld fixture;
    const EntityId player = NetAdmitPlayer(fixture.Entities, PeerId{ 9 });
    const EntityId body = fixture.Thing();
    const EntityId jeep = fixture.Thing();
    fixture.Entities.TryGet<NetPlayerControl>(player)->Body = body;
    NetPossess(fixture.Entities, player, jeep);

    NetRetirePlayer(fixture.Entities, player);

    EXPECT_FALSE(fixture.Entities.IsAlive(player));
    EXPECT_TRUE(fixture.Entities.IsAlive(jeep))
        << "a driver quitting destroyed the vehicle they were sitting in";
    EXPECT_TRUE(fixture.Entities.IsAlive(body))
        << "retirement destroyed a body the game had not decided about";
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(jeep), nullptr)
        << "the vehicle still reads a source that will never fill again";
    EXPECT_FALSE(fixture.Entities.HasComponent<NetDrivenBy>(jeep))
        << "the vehicle is still reported as driven by somebody who has gone";
}

TEST(NetPlayer, RetiringAPeerThatWasNeverAdmittedDoesNothing)
{
    PlayerWorld fixture;

    NetRetirePlayer(fixture.Entities,
                    NetPlayerForPeer(fixture.Entities, PeerId{ 12 }));

    SUCCEED() << "retiring nobody is a question with no subject, not a crash";
}
