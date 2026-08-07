#include <gtest/gtest.h>

#include <net/LoopbackTransport.h>
#include <net/SimulatedTransport.h>
#include <net/UdpTransport.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::vector<std::byte> Bytes(std::string_view text)
    {
        std::vector<std::byte> out(text.size());
        std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    std::string Text(std::span<const std::byte> payload)
    {
        return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    // Drains one transport into owned copies, because the seam only promises
    // the returned payloads stay valid until the next call.
    std::vector<std::string> DrainText(INetTransport& transport)
    {
        std::vector<std::string> out;
        for (const NetDatagram& datagram : transport.Receive())
            out.push_back(Text(datagram.Payload));
        return out;
    }
}

TEST(LoopbackTransport, DeliversToTheBoundPeer)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport client(network);

    ASSERT_TRUE(host.Open(27500));
    ASSERT_TRUE(client.Open(0));
    EXPECT_EQ(host.LocalAddress(), NetAddressLoopback(27500));
    EXPECT_TRUE(client.LocalAddress().IsValid());

    const auto payload = Bytes("hello");
    ASSERT_TRUE(client.Send(host.LocalAddress(), payload));

    const auto received = host.Receive();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].From, client.LocalAddress());
    EXPECT_EQ(Text(received[0].Payload), "hello");
}

TEST(LoopbackTransport, ReceiveDrainsSoASecondCallIsEmpty)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport client(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(client.Open(0));

    ASSERT_TRUE(client.Send(host.LocalAddress(), Bytes("one")));
    EXPECT_EQ(DrainText(host).size(), 1u);
    EXPECT_TRUE(host.Receive().empty());
}

TEST(LoopbackTransport, PreservesSendOrderWithinOneDrain)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport client(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(client.Open(0));

    for (std::string_view text : { "a", "b", "c" })
        ASSERT_TRUE(client.Send(host.LocalAddress(), Bytes(text)));

    EXPECT_EQ(DrainText(host), (std::vector<std::string>{ "a", "b", "c" }));
}

// A datagram to nothing is a silent drop on the wire, so it is one here too;
// reporting failure would make callers write a branch that cannot exist in a
// real session.
TEST(LoopbackTransport, SendToUnboundAddressSucceedsAndGoesNowhere)
{
    LoopbackNetwork network;
    LoopbackTransport client(network);
    ASSERT_TRUE(client.Open(0));

    EXPECT_TRUE(client.Send(NetAddressLoopback(9999), Bytes("into the void")));
}

TEST(LoopbackTransport, RefusesOversizedDatagrams)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport client(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(client.Open(0));

    const std::vector<std::byte> tooBig(kNetMaxDatagramBytes + 1, std::byte{ 0 });
    EXPECT_FALSE(client.Send(host.LocalAddress(), tooBig));

    const std::vector<std::byte> exact(kNetMaxDatagramBytes, std::byte{ 0 });
    EXPECT_TRUE(client.Send(host.LocalAddress(), exact));
}

TEST(LoopbackTransport, RefusesASecondBindOnTheSamePort)
{
    LoopbackNetwork network;
    LoopbackTransport first(network);
    LoopbackTransport second(network);

    ASSERT_TRUE(first.Open(27500));
    EXPECT_FALSE(second.Open(27500));
}

TEST(LoopbackTransport, CloseReleasesThePort)
{
    LoopbackNetwork network;
    {
        LoopbackTransport first(network);
        ASSERT_TRUE(first.Open(27500));
        EXPECT_EQ(network.BoundCount(), 1u);
    }
    EXPECT_EQ(network.BoundCount(), 0u);

    LoopbackTransport second(network);
    EXPECT_TRUE(second.Open(27500));
}

// The impairment schedule is the reliability layer's test instrument, so its
// determinism is itself worth a test: same seed, same outcome, or a failure
// found here is not reproducible from its seed.
TEST(SimulatedTransport, SameSeedProducesTheSameSchedule)
{
    const auto run = [](std::uint64_t seed) {
        LoopbackNetwork network;
        LoopbackTransport host(network);
        LoopbackTransport rawClient(network);
        EXPECT_TRUE(host.Open(0));
        EXPECT_TRUE(rawClient.Open(0));

        SimulatedTransport client(rawClient, NetImpairment{
            .LossPercent = 30,
            .DuplicatePercent = 20,
            .ReorderPercent = 20,
            .Seed = seed,
        });

        for (int i = 0; i < 200; ++i)
            EXPECT_TRUE(client.Send(host.LocalAddress(), Bytes(std::to_string(i))));
        client.Flush();
        return std::tuple{ client.Dropped(), client.Duplicated(), client.Reordered() };
    };

    EXPECT_EQ(run(12345), run(12345));
    EXPECT_NE(run(12345), run(99999));
}

TEST(SimulatedTransport, TotalLossDeliversNothing)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport rawClient(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(rawClient.Open(0));

    SimulatedTransport client(rawClient, NetImpairment{ .LossPercent = 100, .Seed = 7 });

    for (int i = 0; i < 32; ++i)
        EXPECT_TRUE(client.Send(host.LocalAddress(), Bytes("x")));

    EXPECT_TRUE(host.Receive().empty());
    EXPECT_EQ(client.Dropped(), 32u);
}

TEST(SimulatedTransport, NoImpairmentIsATransparentPassThrough)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport rawClient(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(rawClient.Open(0));

    SimulatedTransport client(rawClient, NetImpairment{ .Seed = 3 });

    for (std::string_view text : { "a", "b", "c" })
        ASSERT_TRUE(client.Send(host.LocalAddress(), Bytes(text)));

    EXPECT_EQ(DrainText(host), (std::vector<std::string>{ "a", "b", "c" }));
    EXPECT_EQ(client.Dropped(), 0u);
    EXPECT_EQ(client.Duplicated(), 0u);
}

TEST(SimulatedTransport, DuplicationDeliversTheSamePayloadTwice)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport rawClient(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(rawClient.Open(0));

    SimulatedTransport client(rawClient, NetImpairment{ .DuplicatePercent = 100, .Seed = 11 });

    ASSERT_TRUE(client.Send(host.LocalAddress(), Bytes("dup")));

    EXPECT_EQ(DrainText(host), (std::vector<std::string>{ "dup", "dup" }));
    EXPECT_EQ(client.Duplicated(), 1u);
}

// Reordering has to actually reorder, not merely delay: a receiver that never
// sees a later sequence before an earlier one is not being tested.
TEST(SimulatedTransport, ReorderingDeliversLaterBeforeEarlier)
{
    LoopbackNetwork network;
    LoopbackTransport host(network);
    LoopbackTransport rawClient(network);
    ASSERT_TRUE(host.Open(0));
    ASSERT_TRUE(rawClient.Open(0));

    // The first send is held, the second passes and releases it behind itself.
    SimulatedTransport client(rawClient, NetImpairment{ .ReorderPercent = 100, .Seed = 5 });
    ASSERT_TRUE(client.Send(host.LocalAddress(), Bytes("first")));
    EXPECT_TRUE(host.Receive().empty()) << "the held datagram must not arrive yet";

    SimulatedTransport passthrough(rawClient, NetImpairment{ .Seed = 5 });
    ASSERT_TRUE(passthrough.Send(host.LocalAddress(), Bytes("second")));
    client.Flush();

    EXPECT_EQ(DrainText(host), (std::vector<std::string>{ "second", "first" }));
}

// The real socket, over loopback. Kept to one round trip: this is the only test
// that touches the kernel, and everything above it runs in process.
TEST(UdpTransport, RoundTripsOverRealLoopback)
{
    UdpTransport host;
    UdpTransport client;
    ASSERT_TRUE(host.Open(0)) << "ephemeral bind failed";
    ASSERT_TRUE(client.Open(0));
    ASSERT_TRUE(host.LocalAddress().IsValid());
    EXPECT_NE(host.LocalAddress().Port, 0);

    ASSERT_TRUE(client.Send(host.LocalAddress(), Bytes("over the wire")));

    // Nonblocking: the datagram is usually there immediately over loopback, but
    // the socket is under no obligation to have it on the first drain.
    std::vector<std::string> received;
    for (int attempt = 0; attempt < 100 && received.empty(); ++attempt)
        received = DrainText(host);

    ASSERT_EQ(received.size(), 1u) << "loopback datagram never arrived";
    EXPECT_EQ(received[0], "over the wire");
}

TEST(UdpTransport, RefusesOversizedAndUnopenedSends)
{
    UdpTransport transport;
    EXPECT_FALSE(transport.Send(NetAddressLoopback(1234), Bytes("closed")));
    EXPECT_TRUE(transport.Receive().empty());

    ASSERT_TRUE(transport.Open(0));
    const std::vector<std::byte> tooBig(kNetMaxDatagramBytes + 1, std::byte{ 0 });
    EXPECT_FALSE(transport.Send(NetAddressLoopback(1234), tooBig));
}

TEST(UdpTransport, RefusesASecondBindOnTheSamePort)
{
    UdpTransport first;
    ASSERT_TRUE(first.Open(0));
    const std::uint16_t port = first.LocalAddress().Port;

    UdpTransport second;
    EXPECT_FALSE(second.Open(port));
}
