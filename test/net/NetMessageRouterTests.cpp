#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/LoopbackTransport.h>
#include <net/NetMessageRouter.h>
#include <net/NetStats.h>

#include <cstring>
#include <string>
#include <vector>

//=============================================================================
// The seam a game's own messages arrive through
//
// Transport reliability was already hidden. What was not was everything
// between a datagram and a handler: which byte a game may use, which direction
// a kind is legal in, who the sender is, and whether any of it is counted.
//=============================================================================

namespace
{
    constexpr std::uint8_t kInteract = kNetFirstGamePayloadKind;
    constexpr std::uint8_t kCosmetic = kNetFirstGamePayloadKind + 1;

    struct Recorder
    {
        int Calls = 0;
        PeerId LastFrom;
        std::string LastBody;
        bool Accept = true;

        static bool Handle(void* context, const NetMessageContext& message)
        {
            auto& self = *static_cast<Recorder*>(context);
            ++self.Calls;
            self.LastFrom = message.From;
            self.LastBody.assign(
                reinterpret_cast<const char*>(message.Body.data()),
                message.Body.size());
            return self.Accept;
        }
    };

    std::vector<std::byte> Bytes(std::string_view text)
    {
        std::vector<std::byte> out(text.size());
        std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    struct RouterFixture
    {
        World Entities;
        NetMessageRouter Router;
        Recorder Handler;

        NetMessageContext Message(PeerId from, std::span<const std::byte> body)
        {
            return NetMessageContext{
                .From = from, .Entities = Entities, .Objects = nullptr, .Body = body
            };
        }
    };
}

//-----------------------------------------------------------------------------
// Which byte a game may use
//-----------------------------------------------------------------------------

// Below the game range the engine's own decoders run first, so a binding there
// is a handler that is never reached. Refused rather than accepted and shadowed.
TEST(NetMessageRouter, AKindTheEngineOwnsCannotBeBound)
{
    RouterFixture fixture;
    for (std::uint8_t kind : { std::uint8_t{ 0 }, std::uint8_t{ 1 },
                               std::uint8_t{ 2 }, std::uint8_t{ 3 },
                               std::uint8_t{ 4 }, std::uint8_t{ 63 } })
    {
        EXPECT_FALSE(fixture.Router.Bind(kind, NetMessageDirection::ClientToAuthority,
                                         &Recorder::Handle, &fixture.Handler))
            << "bound engine kind " << int(kind);
    }
    EXPECT_EQ(fixture.Router.BoundKinds(), 0u);
}

// Two answers to one question is not a configuration, and the second silently
// winning is how a message ends up handled by the wrong feature.
TEST(NetMessageRouter, ASecondBindingOnOneKindIsRefused)
{
    RouterFixture fixture;
    Recorder other;
    ASSERT_TRUE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                    &Recorder::Handle, &fixture.Handler));
    EXPECT_FALSE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                     &Recorder::Handle, &other));

    const std::vector<std::byte> body = Bytes("x");
    EXPECT_TRUE(fixture.Router.Route(NetSessionRole::Host, kInteract,
                                     fixture.Message(PeerId{ 1 }, body)));
    EXPECT_EQ(fixture.Handler.Calls, 1);
    EXPECT_EQ(other.Calls, 0) << "the losing binding answered anyway";
}

TEST(NetMessageRouter, ANullHandlerIsRefused)
{
    RouterFixture fixture;
    EXPECT_FALSE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                     nullptr, &fixture.Handler));
    EXPECT_FALSE(fixture.Router.IsBound(kInteract));
}

TEST(NetMessageRouter, AKindNothingBoundIsRefusedRatherThanIgnored)
{
    RouterFixture fixture;
    const std::vector<std::byte> body = Bytes("x");
    EXPECT_FALSE(fixture.Router.Route(NetSessionRole::Host, kCosmetic,
                                      fixture.Message(PeerId{ 1 }, body)));
}

//-----------------------------------------------------------------------------
// Which direction it may travel
//-----------------------------------------------------------------------------

// A client sending an authority-to-client kind is a different build or
// something probing the port, and recognising that is not a handler's job.
TEST(NetMessageRouter, AKindArrivingFromTheWrongDirectionNeverReachesItsHandler)
{
    RouterFixture fixture;
    ASSERT_TRUE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                    &Recorder::Handle, &fixture.Handler));

    const std::vector<std::byte> body = Bytes("open");
    // A client-to-authority kind arriving at a client.
    EXPECT_FALSE(fixture.Router.Route(NetSessionRole::Client, kInteract,
                                      fixture.Message(PeerId{ 1 }, body)));
    EXPECT_EQ(fixture.Handler.Calls, 0);

    // And the same kind at the authority, which is where it belongs.
    EXPECT_TRUE(fixture.Router.Route(NetSessionRole::Host, kInteract,
                                     fixture.Message(PeerId{ 1 }, body)));
    EXPECT_EQ(fixture.Handler.Calls, 1);
}

TEST(NetMessageRouter, AnAuthorityToClientKindIsRefusedAtTheAuthority)
{
    RouterFixture fixture;
    ASSERT_TRUE(fixture.Router.Bind(kCosmetic, NetMessageDirection::AuthorityToClient,
                                    &Recorder::Handle, &fixture.Handler));

    const std::vector<std::byte> body = Bytes("spark");
    EXPECT_FALSE(fixture.Router.Route(NetSessionRole::Host, kCosmetic,
                                      fixture.Message(PeerId{ 1 }, body)));
    EXPECT_EQ(fixture.Handler.Calls, 0);
    EXPECT_TRUE(fixture.Router.Route(NetSessionRole::Client, kCosmetic,
                                     fixture.Message(PeerId{ 1 }, body)));
}

//-----------------------------------------------------------------------------
// Who sent it
//-----------------------------------------------------------------------------

// The one fact a message is never allowed to claim about itself. The body here
// says one peer and the delivery says another; the handler is told the
// delivery's.
TEST(NetMessageRouter, TheSenderIsTheSessionsAndNotTheMessages)
{
    RouterFixture fixture;
    ASSERT_TRUE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                    &Recorder::Handle, &fixture.Handler));

    const std::vector<std::byte> body = Bytes("I am peer 99");
    EXPECT_TRUE(fixture.Router.Route(NetSessionRole::Host, kInteract,
                                     fixture.Message(PeerId{ 4 }, body)));
    EXPECT_EQ(fixture.Handler.LastFrom, PeerId{ 4 });
}

TEST(NetMessageRouter, AHandlerSeesTheBodyWithoutTheKindByte)
{
    RouterFixture fixture;
    ASSERT_TRUE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                    &Recorder::Handle, &fixture.Handler));

    const std::vector<std::byte> body = Bytes("door-7");
    ASSERT_TRUE(fixture.Router.Route(NetSessionRole::Host, kInteract,
                                     fixture.Message(PeerId{ 1 }, body)));
    EXPECT_EQ(fixture.Handler.LastBody, "door-7");
}

// A handler that refuses is the same answer to the session as one that would
// not decode: this peer is sending things this build will not accept.
TEST(NetMessageRouter, AHandlerRefusingIsReportedToTheCaller)
{
    RouterFixture fixture;
    fixture.Handler.Accept = false;
    ASSERT_TRUE(fixture.Router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                                    &Recorder::Handle, &fixture.Handler));

    const std::vector<std::byte> body = Bytes("nonsense");
    EXPECT_FALSE(fixture.Router.Route(NetSessionRole::Host, kInteract,
                                      fixture.Message(PeerId{ 1 }, body)));
    EXPECT_EQ(fixture.Handler.Calls, 1) << "the handler is what decides";
}

//-----------------------------------------------------------------------------
// Sending
//-----------------------------------------------------------------------------

namespace
{
    // A host and a client on one in-process network, stepped by hand -- the
    // same shape the session tests use, because the handshake is a state
    // machine and these need it completed rather than simulated.
    struct SessionPair
    {
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport ClientTransport{ Network };
        NetSession Host{ HostTransport };
        NetSession Client{ ClientTransport };
        NetStats Traffic;
        double Now = 0.0;

        SessionPair()
        {
            const NetIdentity identity{};
            EXPECT_TRUE(Host.Host(0, identity));
            EXPECT_TRUE(Client.Connect(Host.LocalAddress(), identity));
            for (int frame = 0; frame < 16 && !Client.IsConnected(); ++frame)
                Step();
        }

        std::vector<NetSession::Delivery> Step()
        {
            Now += 1.0 / 60.0;
            std::vector<NetSession::Delivery> hostInbox = Host.Pump(Now);
            (void)Client.Pump(Now);
            Host.Flush(Now);
            Client.Flush(Now);
            return hostInbox;
        }
    };
}

// The reason outbound game traffic appeared nowhere: the frame phase counts the
// three kinds it sends itself, and a game sending its own left no trace in the
// numbers anyone reads to ask what grew.
TEST(NetMessageSend, WhatAGameSendsIsCounted)
{
    SessionPair pair;
    ASSERT_TRUE(pair.Client.IsConnected());

    // Starts the window the rates below are measured over; the first sample
    // only starts the clock.
    pair.Traffic.Sample(0.0);

    const std::vector<std::byte> body = Bytes("open");
    const std::size_t sent = NetSendToAuthority(
        pair.Client, NetChannelKind::ReliableOrdered, kInteract, body, &pair.Traffic);

    ASSERT_GT(sent, 0u);
    EXPECT_EQ(sent, body.size() + kNetPayloadKindBytes)
        << "the kind byte is the sender's job and it went missing";

    // One second later, so a per-second rate is the bytes themselves.
    pair.Traffic.Sample(1.0);
    EXPECT_EQ(pair.Traffic.Out(NetTrafficKind::Game).Bytes,
              static_cast<double>(sent));
    EXPECT_EQ(pair.Traffic.Out(NetTrafficKind::Other).Bytes, 0.0)
        << "a game's traffic landed in the handshake's bucket";
}

TEST(NetMessageSend, AKindTheEngineOwnsIsNeverSent)
{
    SessionPair pair;
    const std::vector<std::byte> body = Bytes("x");
    EXPECT_EQ(NetSendToAuthority(pair.Client, NetChannelKind::ReliableOrdered,
                                 static_cast<std::uint8_t>(NetPayloadKind::Snapshot),
                                 body, &pair.Traffic),
              0u);
    EXPECT_EQ(pair.Traffic.Out(NetTrafficKind::Game).Bytes, 0u);
}

// Each direction has its own function because who a message goes to differs by
// role in a way a PeerId argument hides.
TEST(NetMessageSend, EachSenderRefusesTheRoleItIsNotFor)
{
    SessionPair pair;
    const std::vector<std::byte> body = Bytes("x");

    EXPECT_EQ(NetSendToAuthority(pair.Host, NetChannelKind::ReliableOrdered,
                                 kInteract, body, &pair.Traffic), 0u);
    EXPECT_EQ(NetSendToPeer(pair.Client, PeerId{ 1 }, NetChannelKind::ReliableOrdered,
                            kInteract, body, &pair.Traffic), 0u);
    EXPECT_EQ(NetBroadcastToPeers(pair.Client, NetChannelKind::UnreliableSequenced,
                                  kCosmetic, body, &pair.Traffic), 0u);
}

// End to end: a game's bytes cross a real session and come back out of the
// router, with the sender the session verified rather than the one in the body.
TEST(NetMessageSend, AMessageCrossesASessionAndReachesItsHandler)
{
    SessionPair pair;
    ASSERT_TRUE(pair.Client.IsConnected());

    World world;
    NetMessageRouter router;
    Recorder handler;
    ASSERT_TRUE(router.Bind(kInteract, NetMessageDirection::ClientToAuthority,
                            &Recorder::Handle, &handler));

    const std::vector<std::byte> body = Bytes("door-7");
    ASSERT_GT(NetSendToAuthority(pair.Client, NetChannelKind::ReliableOrdered,
                                 kInteract, body, &pair.Traffic), 0u);

    // A step pumps and then flushes, so what the client queued leaves at the
    // end of one step and reaches the host on the next.
    std::vector<NetSession::Delivery> arrived;
    for (int frame = 0; frame < 8 && arrived.empty(); ++frame)
        arrived = pair.Step();
    ASSERT_FALSE(arrived.empty());

    bool answered = false;
    for (const NetSession::Delivery& delivery : arrived)
    {
        if (delivery.Payload.empty())
            continue;
        const std::uint8_t kind = static_cast<std::uint8_t>(delivery.Payload[0]);
        if (!router.IsBound(kind))
            continue;
        const NetMessageContext message{
            .From = delivery.From,
            .Entities = world,
            .Objects = nullptr,
            .Body = std::span<const std::byte>(delivery.Payload)
                        .subspan(kNetPayloadKindBytes),
        };
        answered = router.Route(NetSessionRole::Host, kind, message);
    }

    EXPECT_TRUE(answered);
    EXPECT_EQ(handler.LastBody, "door-7");
    EXPECT_TRUE(handler.LastFrom.IsValid());
}
