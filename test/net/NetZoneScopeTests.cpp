#include <gtest/gtest.h>

#include <net/LoopbackTransport.h>
#include <net/NetProtocol.h>
#include <net/NetSession.h>
#include <net/NetZoneScope.h>
#include <net/ReplicationRuntime.h>

#include <array>
#include <cstddef>
#include <vector>

//=============================================================================
// Grant, ack, revoke
//
// The negotiation that lets an authority stream a world without sending a peer
// state for a room it has not loaded. These cover the wire form and the state
// machine; what the writer does with the answer is the snapshot's own coverage.
//=============================================================================

namespace
{
    constexpr ZoneId kHub{ 0xa1 };
    constexpr ZoneId kHallway{ 0xa2 };
    constexpr ZoneId kArena{ 0xa3 };

    std::vector<std::byte> EncodeUpdate(ZoneId zone, NetZoneScopeVerb verb)
    {
        std::array<std::byte, 64> scratch{};
        const std::size_t size =
            NetEncodeZoneScopeUpdate(NetZoneScopeUpdate{ .Zone = zone, .Verb = verb },
                                     scratch);
        return { scratch.begin(), scratch.begin() + size };
    }

    std::vector<std::byte> EncodeAck(ZoneId zone)
    {
        std::array<std::byte, 64> scratch{};
        const std::size_t size = NetEncodeZoneAck(zone, scratch);
        return { scratch.begin(), scratch.begin() + size };
    }
}

//-----------------------------------------------------------------------------
// Wire
//-----------------------------------------------------------------------------

TEST(NetZoneScopeWire, AGrantRoundTrips)
{
    const std::vector<std::byte> bytes = EncodeUpdate(kHub, NetZoneScopeVerb::Grant);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(static_cast<NetPayloadKind>(bytes[0]), NetPayloadKind::ZoneScope);

    NetZoneScopeUpdate decoded;
    ASSERT_TRUE(NetDecodeZoneScopeUpdate(bytes, decoded));
    EXPECT_EQ(decoded.Zone, kHub);
    EXPECT_EQ(decoded.Verb, NetZoneScopeVerb::Grant);
}

TEST(NetZoneScopeWire, ARevokeRoundTrips)
{
    const std::vector<std::byte> bytes = EncodeUpdate(kArena, NetZoneScopeVerb::Revoke);
    NetZoneScopeUpdate decoded;
    ASSERT_TRUE(NetDecodeZoneScopeUpdate(bytes, decoded));
    EXPECT_EQ(decoded.Zone, kArena);
    EXPECT_EQ(decoded.Verb, NetZoneScopeVerb::Revoke);
}

TEST(NetZoneScopeWire, AnAckRoundTrips)
{
    const std::vector<std::byte> bytes = EncodeAck(kHallway);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(static_cast<NetPayloadKind>(bytes[0]), NetPayloadKind::ZoneAck);

    ZoneId decoded;
    ASSERT_TRUE(NetDecodeZoneAck(bytes, decoded));
    EXPECT_EQ(decoded, kHallway);
}

// The two directions must not be readable as each other. They are the same
// shape on the wire and only the kind byte separates them.
TEST(NetZoneScopeWire, TheTwoDirectionsDoNotDecodeAsEachOther)
{
    const std::vector<std::byte> grant = EncodeUpdate(kHub, NetZoneScopeVerb::Grant);
    const std::vector<std::byte> ack = EncodeAck(kHub);

    ZoneId zone;
    EXPECT_FALSE(NetDecodeZoneAck(grant, zone));

    NetZoneScopeUpdate update;
    EXPECT_FALSE(NetDecodeZoneScopeUpdate(ack, update));
}

// There is no zone zero to grant, so the encoder refuses one rather than
// leaving every decoder to notice.
TEST(NetZoneScopeWire, TheInvalidZoneIsNotEncodable)
{
    std::array<std::byte, 64> scratch{};
    EXPECT_EQ(NetEncodeZoneScopeUpdate(
                  NetZoneScopeUpdate{ .Zone = ZoneId{}, .Verb = NetZoneScopeVerb::Grant },
                  scratch),
              0u);
    EXPECT_EQ(NetEncodeZoneAck(ZoneId{}, scratch), 0u);
}

TEST(NetZoneScopeWire, AZoneZeroOnTheWireIsRefused)
{
    std::vector<std::byte> bytes = EncodeUpdate(kHub, NetZoneScopeVerb::Grant);
    for (std::size_t at = 2; at < bytes.size(); ++at)
        bytes[at] = std::byte{ 0 };

    NetZoneScopeUpdate decoded;
    EXPECT_FALSE(NetDecodeZoneScopeUpdate(bytes, decoded));
}

TEST(NetZoneScopeWire, AnUnknownVerbIsRefused)
{
    std::vector<std::byte> bytes = EncodeUpdate(kHub, NetZoneScopeVerb::Grant);
    bytes[1] = std::byte{ 7 };

    NetZoneScopeUpdate decoded;
    EXPECT_FALSE(NetDecodeZoneScopeUpdate(bytes, decoded));
}

TEST(NetZoneScopeWire, TruncationAndTrailingBytesAreBothRefused)
{
    const std::vector<std::byte> grant = EncodeUpdate(kHub, NetZoneScopeVerb::Grant);
    for (std::size_t keep = 0; keep < grant.size(); ++keep)
    {
        NetZoneScopeUpdate decoded;
        EXPECT_FALSE(NetDecodeZoneScopeUpdate(
            std::span<const std::byte>(grant).subspan(0, keep), decoded))
            << "accepted a message cut at " << keep;
    }

    std::vector<std::byte> extra = grant;
    extra.push_back(std::byte{ 0xff });
    NetZoneScopeUpdate decoded;
    EXPECT_FALSE(NetDecodeZoneScopeUpdate(extra, decoded));

    const std::vector<std::byte> ack = EncodeAck(kHub);
    for (std::size_t keep = 0; keep < ack.size(); ++keep)
    {
        ZoneId zone;
        EXPECT_FALSE(NetDecodeZoneAck(
            std::span<const std::byte>(ack).subspan(0, keep), zone))
            << "accepted an ack cut at " << keep;
    }

    std::vector<std::byte> longAck = ack;
    longAck.push_back(std::byte{ 0xff });
    ZoneId zone;
    EXPECT_FALSE(NetDecodeZoneAck(longAck, zone));
}

//-----------------------------------------------------------------------------
// The state machine
//-----------------------------------------------------------------------------

// The whole point: a grant is an instruction to load, not permission to
// receive. State flows only after the peer says it has somewhere to put it.
TEST(NetZoneScope, AGrantAloneDoesNotOpenAZone)
{
    NetZoneScope scope;
    EXPECT_TRUE(scope.Grant(kHub));

    EXPECT_EQ(scope.StateOf(kHub), NetZoneScopeState::Granted);
    EXPECT_FALSE(scope.CanReceive(kHub))
        << "state was sendable for a room the peer is still loading";
}

TEST(NetZoneScope, AnAckOpensIt)
{
    NetZoneScope scope;
    (void)scope.Grant(kHub);
    EXPECT_TRUE(scope.Acknowledge(kHub));

    EXPECT_EQ(scope.StateOf(kHub), NetZoneScopeState::Acked);
    EXPECT_TRUE(scope.CanReceive(kHub));
}

// The persistent partition is not a zone and no grant gates it. A session's own
// spawned entities live there, so gating it would mean a player could not be
// told about the pawn they are driving.
TEST(NetZoneScope, ThePersistentPartitionNeedsNoGrant)
{
    NetZoneScope scope;
    EXPECT_TRUE(scope.CanReceive(ZoneId{}));
}

TEST(NetZoneScope, AZoneNobodyGrantedIsClosed)
{
    NetZoneScope scope;
    EXPECT_EQ(scope.StateOf(kArena), NetZoneScopeState::None);
    EXPECT_FALSE(scope.CanReceive(kArena));
}

// A peer claiming a room nobody offered it is a protocol violation, not a
// no-op: accepting it is how a client talks its way into state for a zone the
// authority never decided it should see.
TEST(NetZoneScope, AnAckForAZoneNobodyGrantedIsRefused)
{
    NetZoneScope scope;
    EXPECT_FALSE(scope.Acknowledge(kArena));
    EXPECT_FALSE(scope.CanReceive(kArena));
}

// Answering an honest peer with a strike is worse than answering it with
// nothing.
TEST(NetZoneScope, AckingTwiceIsNotAViolation)
{
    NetZoneScope scope;
    (void)scope.Grant(kHub);
    EXPECT_TRUE(scope.Acknowledge(kHub));
    EXPECT_TRUE(scope.Acknowledge(kHub));
    EXPECT_TRUE(scope.CanReceive(kHub));
}

// Re-granting is not news. Sending it again would be an instruction to load a
// room the peer is in the middle of loading.
TEST(NetZoneScope, RegrantingChangesNothing)
{
    NetZoneScope scope;
    EXPECT_TRUE(scope.Grant(kHub));
    EXPECT_FALSE(scope.Grant(kHub));

    (void)scope.Acknowledge(kHub);
    EXPECT_FALSE(scope.Grant(kHub)) << "a grant reopened a zone the peer already holds";
    EXPECT_EQ(scope.StateOf(kHub), NetZoneScopeState::Acked)
        << "a redundant grant put an acked zone back into loading";
}

// Revoking closes the zone immediately rather than waiting to be answered. The
// authority must stop filling a room the moment it decides the peer should let
// it go; there is nothing to confirm, because unloading cannot fail.
TEST(NetZoneScope, RevokingClosesItAtOnce)
{
    NetZoneScope scope;
    (void)scope.Grant(kHub);
    (void)scope.Acknowledge(kHub);
    ASSERT_TRUE(scope.CanReceive(kHub));

    EXPECT_TRUE(scope.Revoke(kHub));
    EXPECT_FALSE(scope.CanReceive(kHub));
    EXPECT_EQ(scope.StateOf(kHub), NetZoneScopeState::None);
    EXPECT_EQ(scope.Size(), 0u);
}

TEST(NetZoneScope, RevokingWhatWasNeverGrantedIsNotNews)
{
    NetZoneScope scope;
    EXPECT_FALSE(scope.Revoke(kArena));
}

// A room let go and offered again is loaded again. The peer no longer has it,
// so treating the second grant as still-acked would send state into a partition
// that is gone.
TEST(NetZoneScope, AZoneGrantedAgainAfterARevokeMustBeAckedAgain)
{
    NetZoneScope scope;
    (void)scope.Grant(kHub);
    (void)scope.Acknowledge(kHub);
    (void)scope.Revoke(kHub);

    EXPECT_TRUE(scope.Grant(kHub));
    EXPECT_EQ(scope.StateOf(kHub), NetZoneScopeState::Granted);
    EXPECT_FALSE(scope.CanReceive(kHub));
}

TEST(NetZoneScope, ZonesAreHeldInAStableOrder)
{
    NetZoneScope scope;
    (void)scope.Grant(kArena);
    (void)scope.Grant(kHub);
    (void)scope.Grant(kHallway);

    ASSERT_EQ(scope.Size(), 3u);
    EXPECT_EQ(scope.Entries()[0].Zone, kHub);
    EXPECT_EQ(scope.Entries()[1].Zone, kHallway);
    EXPECT_EQ(scope.Entries()[2].Zone, kArena);
}

TEST(NetZoneScope, OneZoneClosingLeavesTheOthersAlone)
{
    NetZoneScope scope;
    (void)scope.Grant(kHub);
    (void)scope.Grant(kHallway);
    (void)scope.Grant(kArena);
    (void)scope.Acknowledge(kHub);
    (void)scope.Acknowledge(kArena);

    (void)scope.Revoke(kHallway);

    EXPECT_TRUE(scope.CanReceive(kHub));
    EXPECT_TRUE(scope.CanReceive(kArena));
    EXPECT_FALSE(scope.CanReceive(kHallway));
    EXPECT_EQ(scope.Size(), 2u);
}

//-----------------------------------------------------------------------------
// The authority side
//
// Reconciling each peer's scope against what it is interested in, over real
// sessions, so what a peer is told is read off the wire rather than off the
// authority's own bookkeeping.
//-----------------------------------------------------------------------------

namespace
{
    NetIdentity ScopeIdentity()
    {
        return NetIdentity{
            .ModuleFingerprint = 0x5c0be,
            .ReplicationTableHash = 0x20e5,
            .WorldIdentity = 0,
            .FixedTickRateMilliHz = 60000,
        };
    }

    // A host and two clients, stepped by hand. Two because the interesting
    // failures are all about one peer's rooms reaching another.
    struct ScopeSession
    {
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport FirstTransport{ Network };
        LoopbackTransport SecondTransport{ Network };
        NetSession Host{ HostTransport };
        NetSession First{ FirstTransport };
        NetSession Second{ SecondTransport };
        ReplicationRuntime Replication;
        double Now = 0.0;

        ScopeSession()
        {
            EXPECT_TRUE(Host.Host(0, ScopeIdentity()));
            EXPECT_TRUE(First.Connect(Host.LocalAddress(), ScopeIdentity()));
            EXPECT_TRUE(Second.Connect(Host.LocalAddress(), ScopeIdentity()));
            Step(12);
            EXPECT_TRUE(First.IsConnected());
            EXPECT_TRUE(Second.IsConnected());
            EXPECT_EQ(Host.ConnectedPeers().size(), 2u);
        }

        void Step(int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                Now += 1.0 / 60.0;
                (void)Host.Pump(Now);
                Received(First, FirstInbox);
                Received(Second, SecondInbox);
                Host.Flush(Now);
                First.Flush(Now);
                Second.Flush(Now);
            }
        }

        // Every zone-scope message a client has been handed, in arrival order.
        std::vector<NetZoneScopeUpdate> FirstInbox;
        std::vector<NetZoneScopeUpdate> SecondInbox;

        PeerId FirstPeer() const { return Host.ConnectedPeers()[0]; }
        PeerId SecondPeer() const { return Host.ConnectedPeers()[1]; }

        ReplicationRuntime::ZoneScopeStats Reconcile(
            std::span<const NetPeerZoneInterest> interest)
        {
            const ReplicationRuntime::ZoneScopeStats stats =
                Replication.PublishZoneScope(Host, interest);
            Step(4);
            return stats;
        }

    private:
        void Received(NetSession& session, std::vector<NetZoneScopeUpdate>& inbox)
        {
            for (const NetSession::Delivery& delivery : session.Pump(Now))
            {
                NetZoneScopeUpdate update;
                if (NetDecodeZoneScopeUpdate(delivery.Payload, update))
                    inbox.push_back(update);
            }
        }
    };

    std::vector<ZoneId> Zones(std::initializer_list<ZoneId> zones)
    {
        return { zones };
    }
}

TEST(NetZoneScopePublish, AnInterestedPeerIsGrantedItsZonesOnTheWire)
{
    ScopeSession session;
    const std::vector<ZoneId> wanted = Zones({ kHub, kHallway });
    const NetPeerZoneInterest interest{ .Peer = session.FirstPeer(),
                                        .Zones = wanted };

    const auto stats = session.Reconcile(std::span{ &interest, 1 });

    EXPECT_EQ(stats.Grants, 2u);
    EXPECT_EQ(stats.Revokes, 0u);
    EXPECT_GT(stats.BytesQueued, 0u);

    ASSERT_EQ(session.FirstInbox.size(), 2u);
    EXPECT_EQ(session.FirstInbox[0].Zone, kHub);
    EXPECT_EQ(session.FirstInbox[0].Verb, NetZoneScopeVerb::Grant);
    EXPECT_EQ(session.FirstInbox[1].Zone, kHallway);
}

// A grant is not permission. Until the peer answers, the authority's own record
// says the room is still loading.
TEST(NetZoneScopePublish, AGrantedZoneIsNotOpenUntilThePeerAcks)
{
    ScopeSession session;
    const std::vector<ZoneId> wanted = Zones({ kHub });
    const NetPeerZoneInterest interest{ .Peer = session.FirstPeer(),
                                        .Zones = wanted };
    (void)session.Reconcile(std::span{ &interest, 1 });

    const ReplicationPeerState* baseline =
        session.Replication.PeerBaseline(session.FirstPeer());
    ASSERT_NE(baseline, nullptr);
    EXPECT_FALSE(baseline->Zones().CanReceive(kHub));

    EXPECT_TRUE(session.Replication.AcknowledgeZone(session.FirstPeer(), kHub));
    EXPECT_TRUE(baseline->Zones().CanReceive(kHub));
}

// The rooms one player is near are not the rooms another is near, and the whole
// mechanism is worthless if a grant reaches the wrong peer.
TEST(NetZoneScopePublish, OnePeersZonesDoNotReachAnother)
{
    ScopeSession session;
    const std::vector<ZoneId> here = Zones({ kHub });
    const std::vector<ZoneId> there = Zones({ kArena });
    const NetPeerZoneInterest interest[] = {
        { .Peer = session.FirstPeer(), .Zones = here },
        { .Peer = session.SecondPeer(), .Zones = there },
    };

    (void)session.Reconcile(interest);

    ASSERT_EQ(session.FirstInbox.size(), 1u);
    EXPECT_EQ(session.FirstInbox[0].Zone, kHub);
    ASSERT_EQ(session.SecondInbox.size(), 1u);
    EXPECT_EQ(session.SecondInbox[0].Zone, kArena);
}

// Only differences. A peer standing still costs nothing.
TEST(NetZoneScopePublish, ReconcilingAnUnchangedInterestSetSendsNothing)
{
    ScopeSession session;
    const std::vector<ZoneId> wanted = Zones({ kHub, kHallway });
    const NetPeerZoneInterest interest{ .Peer = session.FirstPeer(),
                                        .Zones = wanted };
    (void)session.Reconcile(std::span{ &interest, 1 });
    session.FirstInbox.clear();

    const auto again = session.Reconcile(std::span{ &interest, 1 });

    EXPECT_EQ(again.Grants, 0u);
    EXPECT_EQ(again.Revokes, 0u);
    EXPECT_EQ(again.BytesQueued, 0u);
    EXPECT_TRUE(session.FirstInbox.empty());
}

// Walking out of a room: the zone leaves the interest set, the peer is told,
// and the authority stops considering it open at once.
TEST(NetZoneScopePublish, AZoneLeavingInterestIsRevoked)
{
    ScopeSession session;
    const std::vector<ZoneId> both = Zones({ kHub, kHallway });
    NetPeerZoneInterest interest{ .Peer = session.FirstPeer(), .Zones = both };
    (void)session.Reconcile(std::span{ &interest, 1 });
    EXPECT_TRUE(session.Replication.AcknowledgeZone(session.FirstPeer(), kHub));
    session.FirstInbox.clear();

    const std::vector<ZoneId> narrowed = Zones({ kHallway });
    interest.Zones = narrowed;
    const auto stats = session.Reconcile(std::span{ &interest, 1 });

    EXPECT_EQ(stats.Revokes, 1u);
    ASSERT_EQ(session.FirstInbox.size(), 1u);
    EXPECT_EQ(session.FirstInbox[0].Zone, kHub);
    EXPECT_EQ(session.FirstInbox[0].Verb, NetZoneScopeVerb::Revoke);

    const ReplicationPeerState* baseline =
        session.Replication.PeerBaseline(session.FirstPeer());
    ASSERT_NE(baseline, nullptr);
    EXPECT_FALSE(baseline->Zones().CanReceive(kHub));
}

// Default deny. A caller that forgot a peer leaves it holding rooms nobody is
// near, and the opposite default fails in exactly the direction this mechanism
// exists to prevent.
TEST(NetZoneScopePublish, APeerMissingFromTheInterestSetHoldsNothing)
{
    ScopeSession session;
    const std::vector<ZoneId> wanted = Zones({ kHub });
    const NetPeerZoneInterest interest{ .Peer = session.FirstPeer(),
                                        .Zones = wanted };
    (void)session.Reconcile(std::span{ &interest, 1 });
    session.FirstInbox.clear();

    const auto stats = session.Reconcile({});

    EXPECT_EQ(stats.Revokes, 1u);
    ASSERT_EQ(session.FirstInbox.size(), 1u);
    EXPECT_EQ(session.FirstInbox[0].Verb, NetZoneScopeVerb::Revoke);
}

// Told what it may let go before it is told to take more on, so a peer at its
// own residency cap is never asked to hold both at once.
TEST(NetZoneScopePublish, ARevokeIsSentBeforeTheGrantThatReplacesIt)
{
    ScopeSession session;
    const std::vector<ZoneId> before = Zones({ kHub });
    NetPeerZoneInterest interest{ .Peer = session.FirstPeer(), .Zones = before };
    (void)session.Reconcile(std::span{ &interest, 1 });
    session.FirstInbox.clear();

    const std::vector<ZoneId> after = Zones({ kArena });
    interest.Zones = after;
    (void)session.Reconcile(std::span{ &interest, 1 });

    ASSERT_EQ(session.FirstInbox.size(), 2u);
    EXPECT_EQ(session.FirstInbox[0].Verb, NetZoneScopeVerb::Revoke);
    EXPECT_EQ(session.FirstInbox[0].Zone, kHub);
    EXPECT_EQ(session.FirstInbox[1].Verb, NetZoneScopeVerb::Grant);
    EXPECT_EQ(session.FirstInbox[1].Zone, kArena);
}

// A peer claiming a room nobody offered it, through the authority's own door.
//
// Granted a different zone first, so the peer has a scope to be refused by. Ask
// before it has one and the refusal comes from the peer being unknown, which is
// a different rule that happens to give the same answer.
TEST(NetZoneScopePublish, AnAckForAnUngrantedZoneIsRefused)
{
    ScopeSession session;
    const std::vector<ZoneId> wanted = Zones({ kHub });
    const NetPeerZoneInterest interest{ .Peer = session.FirstPeer(),
                                        .Zones = wanted };
    (void)session.Reconcile(std::span{ &interest, 1 });
    ASSERT_NE(session.Replication.PeerBaseline(session.FirstPeer()), nullptr);

    EXPECT_FALSE(session.Replication.AcknowledgeZone(session.FirstPeer(), kArena));

    const ReplicationPeerState* baseline =
        session.Replication.PeerBaseline(session.FirstPeer());
    EXPECT_FALSE(baseline->Zones().CanReceive(kArena))
        << "a peer talked its way into a room the authority never offered it";
}

TEST(NetZoneScopePublish, AnAckFromAPeerThisAuthorityDoesNotServeIsRefused)
{
    ScopeSession session;
    EXPECT_FALSE(session.Replication.AcknowledgeZone(PeerId{ 99 }, kHub));
}

// A peer that leaves takes its scope with it, so a recycled peer id cannot
// inherit the rooms its predecessor held open.
TEST(NetZoneScopePublish, ForgettingAPeerForgetsItsZones)
{
    ScopeSession session;
    const std::vector<ZoneId> wanted = Zones({ kHub });
    const NetPeerZoneInterest interest{ .Peer = session.FirstPeer(),
                                        .Zones = wanted };
    (void)session.Reconcile(std::span{ &interest, 1 });
    EXPECT_TRUE(session.Replication.AcknowledgeZone(session.FirstPeer(), kHub));

    session.Replication.ForgetPeer(session.FirstPeer());

    EXPECT_EQ(session.Replication.PeerBaseline(session.FirstPeer()), nullptr);
    EXPECT_FALSE(session.Replication.AcknowledgeZone(session.FirstPeer(), kHub));
}
