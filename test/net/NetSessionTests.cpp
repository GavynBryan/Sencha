#include <gtest/gtest.h>

#include <net/LoopbackTransport.h>
#include <net/NetSession.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::vector<std::byte> Bytes(std::string_view text)
    {
        std::vector<std::byte> out(text.size());
        if (!text.empty())
            std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    std::string Text(std::span<const std::byte> payload)
    {
        return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    NetIdentity SampleIdentity()
    {
        return NetIdentity{
            .ModuleFingerprint = 0xABCDEF,
            .WorldIdentity = 0x123456,
            .FixedTickRateMilliHz = 60000,
        };
    }

    // A host and a client on one in-process network, stepped by hand. No ports,
    // no threads, no wall clock: the handshake is a state machine and this drives
    // it one frame at a time.
    struct Pair
    {
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport ClientTransport{ Network };
        NetSession HostSession{ HostTransport };
        NetSession ClientSession{ ClientTransport };
        double Now = 0.0;

        std::vector<NetSession::Delivery> HostInbox;
        std::vector<NetSession::Delivery> ClientInbox;

        // One frame for both ends: pump, then flush, the order the frame runs.
        void Step(int frames = 1)
        {
            for (int i = 0; i < frames; ++i)
            {
                Now += 1.0 / 60.0;
                for (NetSession::Delivery& delivery : HostSession.Pump(Now))
                    HostInbox.push_back(std::move(delivery));
                for (NetSession::Delivery& delivery : ClientSession.Pump(Now))
                    ClientInbox.push_back(std::move(delivery));
                HostSession.Flush(Now);
                ClientSession.Flush(Now);
            }
        }

        [[nodiscard]] bool StartHost(const NetIdentity& identity = SampleIdentity())
        {
            return HostSession.Host(0, identity);
        }

        [[nodiscard]] bool StartClient(const NetIdentity& identity = SampleIdentity())
        {
            return ClientSession.Connect(HostSession.LocalAddress(), identity);
        }
    };
}

TEST(NetSession, StandaloneDoesNothing)
{
    LoopbackNetwork network;
    LoopbackTransport transport(network);
    NetSession session(transport);

    EXPECT_EQ(session.Role(), NetSessionRole::Standalone);
    EXPECT_FALSE(session.IsConnected());
    EXPECT_TRUE(session.Pump(0.0).empty());
    session.Flush(0.0);  // must not touch a transport that was never opened
    EXPECT_FALSE(transport.IsOpen());
}

TEST(NetSession, ClientCompletesTheHandshakeAndIsAdmitted)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    ASSERT_TRUE(pair.StartClient());

    // Hello, challenge, echo, accept: four legs, so four frames is ample.
    pair.Step(6);

    EXPECT_TRUE(pair.ClientSession.IsConnected());
    EXPECT_TRUE(pair.ClientSession.LocalPeerId().IsValid());
    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 1u);
    EXPECT_EQ(pair.ClientSession.JoinFailure(), NetJoinFailure::None);
}

// The authority holds nothing for an address until that address proves it can
// receive: that is what makes a spoofed source unable to cost the host memory.
TEST(NetSession, HelloAloneAllocatesNoPeerState)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());

    LoopbackTransport spoof(pair.Network);
    ASSERT_TRUE(spoof.Open(0));

    // A flood of hellos from an address that never echoes a cookie.
    std::array<std::byte, 64> scratch{};
    const auto hello = NetEncodeHello(NetHello{}, scratch);
    for (int i = 0; i < 500; ++i)
        ASSERT_TRUE(spoof.Send(pair.HostSession.LocalAddress(), hello));

    pair.Step(2);

    EXPECT_EQ(pair.HostSession.PeerCount(), 0u)
        << "an unverified address must not create peer state";
}

// The cookie is bound to the address it was issued to. Echoing someone else's
// is the exact move a spoofing attacker would make.
TEST(NetSession, ACookieFromAnotherAddressIsRefused)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());

    LoopbackTransport first(pair.Network);
    LoopbackTransport second(pair.Network);
    ASSERT_TRUE(first.Open(0));
    ASSERT_TRUE(second.Open(0));

    std::array<std::byte, 64> scratch{};
    ASSERT_TRUE(first.Send(pair.HostSession.LocalAddress(),
                           NetEncodeHello(NetHello{}, scratch)));
    pair.Step();

    // Take the cookie the host issued to `first`.
    std::vector<std::byte> cookie;
    for (const NetDatagram& datagram : first.Receive())
    {
        NetChallenge challenge;
        if (NetDecodeChallenge(datagram.Payload, challenge) == NetDecodeError::None)
            cookie = challenge.Cookie;
    }
    ASSERT_FALSE(cookie.empty());

    // Replay it from a different address.
    NetCookieEcho echo;
    echo.Cookie = cookie;
    echo.ModuleFingerprint = SampleIdentity().ModuleFingerprint;
    echo.WorldIdentity = SampleIdentity().WorldIdentity;
    echo.FixedTickRateMilliHz = SampleIdentity().FixedTickRateMilliHz;
    std::array<std::byte, 128> echoScratch{};
    ASSERT_TRUE(second.Send(pair.HostSession.LocalAddress(),
                            NetEncodeCookieEcho(echo, echoScratch)));
    pair.Step(2);

    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 0u)
        << "a cookie issued to another address must not admit this one";
    EXPECT_GT(pair.HostSession.Refusals(), 0u);
}

TEST(NetSession, AuthorityRefusesAMismatchedBuild)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());

    NetIdentity wrong = SampleIdentity();
    wrong.ModuleFingerprint = 0xDEADBEEF;
    ASSERT_TRUE(pair.StartClient(wrong));

    pair.Step(6);

    EXPECT_FALSE(pair.ClientSession.IsConnected());
    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 0u);
    EXPECT_EQ(pair.ClientSession.JoinFailure(), NetJoinFailure::Refused);
    EXPECT_EQ(pair.ClientSession.JoinFailureReason(), "game build mismatch");
}

TEST(NetSession, AuthorityRefusesMismatchedWorldContent)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());

    NetIdentity wrong = SampleIdentity();
    wrong.WorldIdentity = 0x999;
    ASSERT_TRUE(pair.StartClient(wrong));
    pair.Step(6);

    EXPECT_EQ(pair.ClientSession.JoinFailureReason(), "world content mismatch");
}

// A tick rate cannot change live, so it is verified at the gate rather than
// synchronized afterwards.
TEST(NetSession, AuthorityRefusesAMismatchedTickRate)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());

    NetIdentity wrong = SampleIdentity();
    wrong.FixedTickRateMilliHz = 30000;
    ASSERT_TRUE(pair.StartClient(wrong));
    pair.Step(6);

    EXPECT_EQ(pair.ClientSession.JoinFailureReason(), "simulation tick rate mismatch");
}

TEST(NetSession, RefusesOnceTheSessionIsFull)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    pair.HostSession.SetMaxPeers(1);
    ASSERT_TRUE(pair.StartClient());
    pair.Step(6);
    ASSERT_EQ(pair.HostSession.ConnectedPeers().size(), 1u);

    LoopbackTransport secondTransport(pair.Network);
    NetSession second(secondTransport);
    ASSERT_TRUE(second.Connect(pair.HostSession.LocalAddress(), SampleIdentity()));

    for (int i = 0; i < 6; ++i)
    {
        pair.Now += 1.0 / 60.0;
        (void)pair.HostSession.Pump(pair.Now);
        (void)second.Pump(pair.Now);
        pair.HostSession.Flush(pair.Now);
        second.Flush(pair.Now);
    }

    EXPECT_FALSE(second.IsConnected());
    EXPECT_EQ(second.JoinFailureReason(), "session full");
    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 1u);
}

TEST(NetSession, MessagesFlowBothWaysOnceAdmitted)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    ASSERT_TRUE(pair.StartClient());
    pair.Step(6);
    ASSERT_TRUE(pair.ClientSession.IsConnected());

    const PeerId peer = pair.HostSession.ConnectedPeers().front();
    ASSERT_TRUE(pair.HostSession.Send(peer, NetChannelKind::ReliableOrdered,
                                      Bytes("from the authority")));
    ASSERT_TRUE(pair.ClientSession.Send(pair.ClientSession.LocalPeerId(),
                                        NetChannelKind::UnreliableSequenced,
                                        Bytes("from the client")));
    pair.Step(3);

    ASSERT_FALSE(pair.ClientInbox.empty());
    EXPECT_EQ(Text(pair.ClientInbox.front().Payload), "from the authority");
    EXPECT_EQ(pair.ClientInbox.front().Channel, NetChannelKind::ReliableOrdered);

    ASSERT_FALSE(pair.HostInbox.empty());
    EXPECT_EQ(Text(pair.HostInbox.front().Payload), "from the client");
    EXPECT_EQ(pair.HostInbox.front().From, peer);
}

TEST(NetSession, BroadcastReachesEveryConnectedPeer)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    ASSERT_TRUE(pair.StartClient());

    LoopbackTransport secondTransport(pair.Network);
    NetSession second(secondTransport);
    ASSERT_TRUE(second.Connect(pair.HostSession.LocalAddress(), SampleIdentity()));

    for (int i = 0; i < 8; ++i)
    {
        pair.Now += 1.0 / 60.0;
        for (NetSession::Delivery& d : pair.HostSession.Pump(pair.Now))
            pair.HostInbox.push_back(std::move(d));
        for (NetSession::Delivery& d : pair.ClientSession.Pump(pair.Now))
            pair.ClientInbox.push_back(std::move(d));
        (void)second.Pump(pair.Now);
        pair.HostSession.Flush(pair.Now);
        pair.ClientSession.Flush(pair.Now);
        second.Flush(pair.Now);
    }
    ASSERT_EQ(pair.HostSession.ConnectedPeers().size(), 2u);

    pair.HostSession.Broadcast(NetChannelKind::ReliableOrdered, Bytes("everyone"));

    std::size_t secondReceived = 0;
    for (int i = 0; i < 4; ++i)
    {
        pair.Now += 1.0 / 60.0;
        (void)pair.HostSession.Pump(pair.Now);
        for (NetSession::Delivery& d : pair.ClientSession.Pump(pair.Now))
            pair.ClientInbox.push_back(std::move(d));
        secondReceived += second.Pump(pair.Now).size();
        pair.HostSession.Flush(pair.Now);
        pair.ClientSession.Flush(pair.Now);
        second.Flush(pair.Now);
    }

    EXPECT_FALSE(pair.ClientInbox.empty());
    EXPECT_EQ(secondReceived, 1u);
}

// A client that hears nothing must give up rather than wait forever: there is
// no window to close on a headless host, and a wedged client is a hung game.
TEST(NetSession, ClientTimesOutWhenTheAuthorityNeverAnswers)
{
    LoopbackNetwork network;
    LoopbackTransport clientTransport(network);
    NetSession client(clientTransport);
    client.SetTimeoutSeconds(1.0);

    // Nothing is bound at this address, so the hello goes nowhere.
    ASSERT_TRUE(client.Connect(NetAddressLoopback(40000), SampleIdentity()));

    for (double now = 0.0; now < 3.0; now += 0.1)
        (void)client.Pump(now);

    EXPECT_FALSE(client.IsConnected());
    EXPECT_EQ(client.JoinFailure(), NetJoinFailure::TimedOut);
    EXPECT_EQ(client.Role(), NetSessionRole::Standalone);
}

// A peer that stops sending is dropped, so its slot, its channel buffers, and
// its place in the peer cap come back.
TEST(NetSession, HostDropsAPeerThatGoesQuiet)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    pair.HostSession.SetTimeoutSeconds(1.0);
    ASSERT_TRUE(pair.StartClient());
    pair.Step(6);
    ASSERT_EQ(pair.HostSession.ConnectedPeers().size(), 1u);

    // The client stops pumping and flushing entirely.
    for (double now = pair.Now; now < pair.Now + 3.0; now += 0.1)
        (void)pair.HostSession.Pump(now);

    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 0u);
    EXPECT_EQ(pair.HostSession.PeerCount(), 0u);
}

TEST(NetSession, DisconnectTellsTheOtherEnd)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    ASSERT_TRUE(pair.StartClient());
    pair.Step(6);
    ASSERT_EQ(pair.HostSession.ConnectedPeers().size(), 1u);

    pair.ClientSession.Disconnect("leaving");
    pair.Step(2);

    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 0u);
    EXPECT_EQ(pair.ClientSession.Role(), NetSessionRole::Standalone);
}

// Garbage from an admitted peer strikes rather than disconnecting on the first
// anomaly, and disconnects once the strikes run out.
TEST(NetSession, MalformedTrafficStrikesAndEventuallyDisconnects)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    ASSERT_TRUE(pair.StartClient());
    pair.Step(6);
    ASSERT_EQ(pair.HostSession.ConnectedPeers().size(), 1u);

    // Too short to be a channel packet, sent from the admitted address.
    const std::vector<std::byte> garbage(3, std::byte{ 0x7F });
    for (std::uint32_t i = 0; i < kNetMaxStrikes; ++i)
    {
        ASSERT_TRUE(pair.ClientTransport.Send(pair.HostSession.LocalAddress(), garbage));
        pair.Now += 1.0 / 60.0;
        (void)pair.HostSession.Pump(pair.Now);
    }

    EXPECT_GE(pair.HostSession.StrikesIssued(), kNetMaxStrikes);
    EXPECT_EQ(pair.HostSession.ConnectedPeers().size(), 0u)
        << "a peer that keeps sending garbage must eventually be dropped";
}

// Off-path injection: the client listens to the authority's address and nothing
// else, which costs nothing and removes a whole class of nuisance.
TEST(NetSession, ClientIgnoresTrafficFromAnyoneButTheAuthority)
{
    Pair pair;
    ASSERT_TRUE(pair.StartHost());
    ASSERT_TRUE(pair.StartClient());
    pair.Step(6);
    ASSERT_TRUE(pair.ClientSession.IsConnected());

    LoopbackTransport stranger(pair.Network);
    ASSERT_TRUE(stranger.Open(0));
    std::array<std::byte, 64> scratch{};
    ASSERT_TRUE(stranger.Send(pair.ClientSession.LocalAddress(),
                              NetEncodeDisconnect(NetDisconnect{ .Reason = "bye" }, scratch)));
    pair.Step(2);

    EXPECT_TRUE(pair.ClientSession.IsConnected())
        << "a stranger must not be able to disconnect a client";
}
