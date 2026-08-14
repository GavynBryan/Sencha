#include <gtest/gtest.h>

#include <net/NetChannel.h>

#include <algorithm>
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

    std::vector<std::string> TextOf(const std::vector<std::vector<std::byte>>& messages)
    {
        std::vector<std::string> out;
        out.reserve(messages.size());
        for (const std::vector<std::byte>& message : messages)
            out.push_back(Text(message));
        return out;
    }

    // A pair of channel sets wired to each other by hand, with a lossy link the
    // test drives explicitly. No transport and no session in the way, so a
    // failure points at the reliability layer and nothing else.
    struct Link
    {
        NetChannelSet A;
        NetChannelSet B;
        double Now = 0.0;

        // Moves A's outbound to B, dropping the packets the caller names by
        // their position in this batch.
        std::vector<std::string> Pump(NetChannelSet& from,
                                      NetChannelSet& to,
                                      std::initializer_list<std::size_t> drop = {})
        {
            std::vector<std::string> delivered;
            std::size_t index = 0;
            for (const NetChannelSet::Packet& packet : from.Drain(Now))
            {
                const bool dropped =
                    std::find(drop.begin(), drop.end(), index) != drop.end();
                ++index;
                if (dropped)
                    continue;

                NetDecodeError error = NetDecodeError::None;
                for (std::string& message : TextOf(to.Receive(packet.Bytes, error)))
                    delivered.push_back(std::move(message));
                EXPECT_EQ(error, NetDecodeError::None);
            }
            return delivered;
        }
    };
}

TEST(NetSequence, ComparesAcrossTheWrap)
{
    EXPECT_TRUE(NetSequenceGreaterThan(1, 0));
    EXPECT_FALSE(NetSequenceGreaterThan(0, 1));
    // The case a plain integer comparison gets backwards: after 65535 comes 0,
    // and reading that as a huge backwards jump throws away everything after.
    EXPECT_TRUE(NetSequenceGreaterThan(0, 65535));
    EXPECT_FALSE(NetSequenceGreaterThan(65535, 0));
    EXPECT_TRUE(NetSequenceGreaterThan(100, 65500));
    EXPECT_FALSE(NetSequenceGreaterThan(65500, 100));
}

TEST(NetChannel, UnreliableDeliversInOrderWhenNothingIsLost)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::UnreliableSequenced, Bytes("one")));
    ASSERT_TRUE(link.A.Send(NetChannelKind::UnreliableSequenced, Bytes("two")));

    EXPECT_EQ(link.Pump(link.A, link.B), (std::vector<std::string>{ "one", "two" }));
}

// The defining property of the class: a snapshot that arrives after a newer one
// is worthless, so delivering it would be worse than dropping it.
TEST(NetChannel, UnreliableDropsAStalePacket)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::UnreliableSequenced, Bytes("old")));
    ASSERT_TRUE(link.A.Send(NetChannelKind::UnreliableSequenced, Bytes("new")));

    std::vector<NetChannelSet::Packet> packets = link.A.Drain(0.0);
    ASSERT_EQ(packets.size(), 2u);

    NetDecodeError error = NetDecodeError::None;
    // Newest first, then the older one behind it.
    EXPECT_EQ(TextOf(link.B.Receive(packets[1].Bytes, error)),
              (std::vector<std::string>{ "new" }));
    EXPECT_TRUE(link.B.Receive(packets[0].Bytes, error).empty());
    EXPECT_EQ(error, NetDecodeError::None);
    EXPECT_EQ(link.B.StaleDropped(), 1u);
}

TEST(NetChannel, UnreliableIsNeverResent)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::UnreliableSequenced, Bytes("gone")));
    EXPECT_EQ(link.A.Drain(0.0).size(), 1u);

    // Well past any resend interval: an unreliable packet is not retained.
    EXPECT_TRUE(link.A.Drain(100.0).empty());
    EXPECT_EQ(link.A.Resends(), 0u);
}

TEST(NetChannel, ReliableDeliversAndIsAcked)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("state")));

    EXPECT_EQ(link.Pump(link.A, link.B), (std::vector<std::string>{ "state" }));
    EXPECT_EQ(link.A.OutstandingReliable(), 1u) << "not acked until B sends back";

    // Any packet from B carries the ack window, so B replying on the unreliable
    // channel is enough to retire A's reliable message.
    ASSERT_TRUE(link.B.Send(NetChannelKind::UnreliableSequenced, Bytes("tick")));
    (void)link.Pump(link.B, link.A);
    EXPECT_EQ(link.A.OutstandingReliable(), 0u);
}

TEST(NetChannel, ReliableResendsUntilAcked)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("must arrive")));
    link.A.SetResendSeconds(0.1);

    // First attempt is dropped on the way.
    EXPECT_TRUE(link.Pump(link.A, link.B, { 0 }).empty());
    EXPECT_EQ(link.A.OutstandingReliable(), 1u);

    // Not yet due.
    link.Now = 0.05;
    EXPECT_TRUE(link.A.Drain(link.Now).empty());

    link.Now = 0.2;
    EXPECT_EQ(link.Pump(link.A, link.B), (std::vector<std::string>{ "must arrive" }));
    EXPECT_EQ(link.A.Resends(), 1u);
}

// A resend whose original did arrive must not be delivered twice: the peer
// resent because the ack was lost, not because the message was.
TEST(NetChannel, ReliableDuplicateIsNotDeliveredTwice)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("once")));

    std::vector<NetChannelSet::Packet> packets = link.A.Drain(0.0);
    ASSERT_EQ(packets.size(), 1u);

    NetDecodeError error = NetDecodeError::None;
    EXPECT_EQ(TextOf(link.B.Receive(packets[0].Bytes, error)),
              (std::vector<std::string>{ "once" }));
    EXPECT_TRUE(link.B.Receive(packets[0].Bytes, error).empty())
        << "a duplicate must be discarded, not redelivered";
    EXPECT_EQ(link.B.DuplicatesIgnored(), 1u);
}

// Ordered means ordered: a later message waits for the earlier one rather than
// being handed up out of order.
TEST(NetChannel, ReliableHoldsAnEarlyMessageUntilTheGapFills)
{
    Link link;
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("first")));
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("second")));

    std::vector<NetChannelSet::Packet> packets = link.A.Drain(0.0);
    ASSERT_EQ(packets.size(), 2u);

    NetDecodeError error = NetDecodeError::None;
    EXPECT_TRUE(link.B.Receive(packets[1].Bytes, error).empty())
        << "second must not be delivered before first";

    EXPECT_EQ(TextOf(link.B.Receive(packets[0].Bytes, error)),
              (std::vector<std::string>{ "first", "second" }))
        << "the held message releases behind the one that filled the gap";
}

TEST(NetChannel, ReliableFragmentsAndReassemblesALargeMessage)
{
    Link link;
    const std::string large(kNetMaxFragmentPayloadBytes * 3 + 17, 'x');
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes(large)));

    std::vector<NetChannelSet::Packet> packets = link.A.Drain(0.0);
    ASSERT_EQ(packets.size(), 4u) << "should have split into four fragments";
    for (const NetChannelSet::Packet& packet : packets)
        EXPECT_LE(packet.Bytes.size(), kNetMaxDatagramBytes);

    NetDecodeError error = NetDecodeError::None;
    std::vector<std::string> delivered;
    for (const NetChannelSet::Packet& packet : packets)
        for (std::string& message : TextOf(link.B.Receive(packet.Bytes, error)))
            delivered.push_back(std::move(message));

    ASSERT_EQ(delivered.size(), 1u);
    EXPECT_EQ(delivered[0], large);
}

// The bug this guards: ordering by packet sequence instead of message id stalls
// forever once a message spans more than one datagram, because the fragments
// consume sequences that never deliver a message of their own.
TEST(NetChannel, AFragmentedMessageDoesNotStallTheOneAfterIt)
{
    Link link;
    const std::string large(kNetMaxFragmentPayloadBytes * 2, 'y');
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes(large)));
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("after")));

    const std::vector<std::string> delivered = link.Pump(link.A, link.B);

    ASSERT_EQ(delivered.size(), 2u);
    EXPECT_EQ(delivered[0], large);
    EXPECT_EQ(delivered[1], "after");
}

TEST(NetChannel, FragmentsArrivingOutOfOrderStillReassemble)
{
    Link link;
    const std::string large(kNetMaxFragmentPayloadBytes * 3, 'z');
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes(large)));

    std::vector<NetChannelSet::Packet> packets = link.A.Drain(0.0);
    ASSERT_EQ(packets.size(), 3u);

    NetDecodeError error = NetDecodeError::None;
    std::vector<std::string> delivered;
    for (std::size_t index : { 2u, 0u, 1u })
        for (std::string& message : TextOf(link.B.Receive(packets[index].Bytes, error)))
            delivered.push_back(std::move(message));

    ASSERT_EQ(delivered.size(), 1u);
    EXPECT_EQ(delivered[0], large);
}

TEST(NetChannel, RefusesAMessageLargerThanTheChannelAllows)
{
    NetChannelSet channel;
    const std::vector<std::byte> tooBigUnreliable(kNetMaxPayloadBytes + 1, std::byte{ 0 });
    EXPECT_FALSE(channel.Send(NetChannelKind::UnreliableSequenced, tooBigUnreliable));

    const std::vector<std::byte> tooBigReliable(kNetMaxReliableMessageBytes + 1, std::byte{ 0 });
    EXPECT_FALSE(channel.Send(NetChannelKind::ReliableOrdered, tooBigReliable));
}

// A peer that stops acking must not be able to grow the outstanding set without
// bound. Refusing is the backpressure; the session disconnects it from there.
TEST(NetChannel, ReliableWindowRefusesOnceFull)
{
    NetChannelSet channel;
    for (std::size_t i = 0; i < kNetReliableWindow; ++i)
        ASSERT_TRUE(channel.Send(NetChannelKind::ReliableOrdered, Bytes("m")))
            << "refused at " << i;

    EXPECT_FALSE(channel.Send(NetChannelKind::ReliableOrdered, Bytes("over")));
    // Retained from Send, not from Drain: the window bounds what is owed an
    // ack, which begins the moment the message is queued.
    EXPECT_EQ(channel.OutstandingReliable(), kNetReliableWindow);
}

TEST(NetChannel, RefusesMalformedPackets)
{
    NetChannelSet channel;
    NetDecodeError error = NetDecodeError::None;

    const std::vector<std::byte> tooShort(kNetPacketHeaderBytes - 1, std::byte{ 0 });
    EXPECT_TRUE(channel.Receive(tooShort, error).empty());
    EXPECT_EQ(error, NetDecodeError::Truncated);

    std::vector<std::byte> unknownChannel(kNetPacketHeaderBytes, std::byte{ 0 });
    unknownChannel[0] = std::byte{ 9 };
    EXPECT_TRUE(channel.Receive(unknownChannel, error).empty());
    EXPECT_EQ(error, NetDecodeError::UnknownMessage);

    // Reliable without its message header.
    std::vector<std::byte> shortReliable(kNetPacketHeaderBytes, std::byte{ 0 });
    shortReliable[0] = static_cast<std::byte>(NetChannelKind::ReliableOrdered);
    EXPECT_TRUE(channel.Receive(shortReliable, error).empty());
    EXPECT_EQ(error, NetDecodeError::Truncated);
}

// Fragment counts and indices reach a vector size and a vector index, so they
// are the fields most worth refusing before use.
TEST(NetChannel, RefusesImpossibleFragmentGeometry)
{
    NetChannelSet channel;
    NetDecodeError error = NetDecodeError::None;

    const auto build = [](std::uint8_t index, std::uint8_t count) {
        std::vector<std::byte> packet(kNetPacketHeaderBytes + kNetReliableHeaderBytes,
                                      std::byte{ 0 });
        packet[0] = static_cast<std::byte>(NetChannelKind::ReliableOrdered);
        packet[kNetPacketHeaderBytes + 2] = static_cast<std::byte>(index);
        packet[kNetPacketHeaderBytes + 3] = static_cast<std::byte>(count);
        return packet;
    };

    EXPECT_TRUE(channel.Receive(build(0, 0), error).empty());
    EXPECT_EQ(error, NetDecodeError::Malformed) << "zero fragments";

    EXPECT_TRUE(channel.Receive(build(3, 2), error).empty());
    EXPECT_EQ(error, NetDecodeError::Malformed) << "index past count";

    EXPECT_TRUE(channel.Receive(build(0, kNetMaxFragments + 1), error).empty());
    EXPECT_EQ(error, NetDecodeError::Malformed) << "count past the cap";
}

// The whole point of the reliable channel, exercised the only way it can be:
// with the link losing things on purpose. Messages are fed as the window frees
// rather than queued all at once, which is both how a session actually behaves
// and what exercises the window against the ack path.
TEST(NetChannel, EverythingReliableArrivesExactlyOnceOverALossyLink)
{
    Link link;
    link.A.SetResendSeconds(0.05);

    constexpr int kMessages = 100;
    int queued = 0;
    std::vector<std::string> delivered;

    for (int round = 0; round < 4000 && delivered.size() < kMessages; ++round)
    {
        link.Now = round * 0.06;

        while (queued < kMessages
               && link.A.Send(NetChannelKind::ReliableOrdered, Bytes(std::to_string(queued))))
        {
            ++queued;
        }

        std::size_t index = 0;
        for (const NetChannelSet::Packet& packet : link.A.Drain(link.Now))
        {
            // Deterministic rather than random, so a failure reproduces
            // exactly. Rotated by the round on purpose: a fixed positional drop
            // against a stable-ordered queue starves whichever message lands on
            // that position forever, which tests the harness and not the
            // protocol.
            const bool drop = ((index++ + static_cast<std::size_t>(round)) % 3) == 1;
            if (drop)
                continue;
            NetDecodeError error = NetDecodeError::None;
            for (std::string& message : TextOf(link.B.Receive(packet.Bytes, error)))
                delivered.push_back(std::move(message));
            EXPECT_EQ(error, NetDecodeError::None);
        }

        // B's acks travel back, retiring what A no longer has to resend. Half
        // of them are dropped too, so the ack path has to tolerate its own loss.
        ASSERT_TRUE(link.B.Send(NetChannelKind::UnreliableSequenced, Bytes("ack")));
        if (round % 2 == 0)
            (void)link.Pump(link.B, link.A);
        else
            (void)link.B.Drain(link.Now);
    }

    ASSERT_EQ(delivered.size(), static_cast<std::size_t>(kMessages))
        << "reliable delivery did not converge under loss";
    for (int i = 0; i < kMessages; ++i)
    {
        EXPECT_EQ(delivered[static_cast<std::size_t>(i)], std::to_string(i))
            << "message " << i << " arrived out of order";
    }

    EXPECT_GT(link.A.Resends(), 0u) << "the loss pattern never forced a resend";
    EXPECT_EQ(link.B.DuplicatesIgnored(), link.B.DuplicatesIgnored())
        << "duplicates are expected here; they must simply not be redelivered";
}

// The invariant the window exists to hold: a message further behind the newest
// than the ack history is deep could never be retired, so the window must not
// let one get there.
TEST(NetChannel, TheSendWindowNeverExceedsWhatAnAckCanRetire)
{
    static_assert(kNetReliableWindow <= kNetAckHistoryBits + 1);

    NetChannelSet channel;
    std::size_t accepted = 0;
    while (channel.Send(NetChannelKind::ReliableOrdered, Bytes("m")))
        ++accepted;

    EXPECT_EQ(accepted, kNetReliableWindow);
    EXPECT_LE(accepted, kNetAckHistoryBits + 1);
}

// Freeing a slot is not the same as freeing the window. This is the case a
// count-only bound misses: one straggler stays unacked while everything behind
// it is acked and replaced, walking the peer's newest sequence off the end of
// the ack history. The delivery itself self-heals -- the straggler is delivered
// whenever a copy finally lands -- so the symptom is not lost data. It is that
// the sender can never retire it: no ack the peer is capable of sending covers
// a sequence that far back, so it resends for the life of the session and the
// outstanding set never drains.
TEST(NetChannel, AStragglerIsRetiredAfterTheWindowHasMovedOn)
{
    Link link;
    link.A.SetResendSeconds(1000.0);  // resends are not what is under test here

    // The first message is lost.
    ASSERT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("straggler")));
    std::vector<NetChannelSet::Packet> stragglerBatch = link.A.Drain(link.Now);
    ASSERT_EQ(stragglerBatch.size(), 1u);

    // Everything behind it is delivered and acked, repeatedly, to drive the
    // peer's newest sequence as far forward as the sender will allow.
    for (int round = 0; round < 100; ++round)
    {
        while (link.A.Send(NetChannelKind::ReliableOrdered, Bytes("filler")))
        {
        }
        link.Now += 0.001;
        (void)link.Pump(link.A, link.B);
        ASSERT_TRUE(link.B.Send(NetChannelKind::UnreliableSequenced, Bytes("ack")));
        (void)link.Pump(link.B, link.A);
    }

    // Now the straggler's copy finally lands, and the peer acks again.
    NetDecodeError error = NetDecodeError::None;
    (void)link.B.Receive(stragglerBatch[0].Bytes, error);
    ASSERT_EQ(error, NetDecodeError::None);
    ASSERT_TRUE(link.B.Send(NetChannelKind::UnreliableSequenced, Bytes("ack")));
    link.Now += 0.001;
    (void)link.Pump(link.B, link.A);

    EXPECT_EQ(link.A.OutstandingReliable(), 0u)
        << "the straggler was walked past the ack horizon and can never be retired";
}

//=============================================================================
// The first reliable message
//
// Found by running a dedicated server with two clients: a client's request
// reached the authority most of the time and, under load, silently did not --
// and the client's channel reported nothing outstanding, so nothing ever
// resent it.
//
// An ack of zero means "I have received no reliable datagram", and a first
// message numbered zero matched it exactly, so the message retired itself
// against a peer that had never seen it -- and, being no longer outstanding,
// was never resent. Every peer sends packets carrying that empty ack before it
// has received anything reliable, and both ends of a session start there.
//=============================================================================

TEST(NetChannel, AFirstReliableMessageIsNotRetiredByAnEmptyAck)
{
    Link link;
    // B has received nothing, so anything it sends carries an empty ack. A
    // snapshot-shaped stream is exactly this: unreliable, constant, and with
    // nothing to report.
    EXPECT_TRUE(link.B.Send(NetChannelKind::UnreliableSequenced, Bytes("tick")));
    EXPECT_TRUE(link.A.Send(NetChannelKind::ReliableOrdered, Bytes("take the turret")));

    // A's first message is lost, and B's empty ack arrives.
    EXPECT_TRUE(link.Pump(link.A, link.B, { 0 }).empty());
    link.Pump(link.B, link.A);

    EXPECT_EQ(link.A.OutstandingReliable(), 1u)
        << "an ack that acknowledged nothing retired the message anyway, so "
           "nothing will ever resend it";

    // And it does arrive, because it was still owed.
    link.Now += 1.0;
    const std::vector<std::string> delivered = link.Pump(link.A, link.B);
    EXPECT_EQ(delivered, std::vector<std::string>{ "take the turret" });
}
