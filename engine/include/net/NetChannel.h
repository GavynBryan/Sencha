#pragma once

#include <net/NetProtocol.h>
#include <net/NetTransport.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

//=============================================================================
// NetChannel
//
// The reliability layer, over datagrams, for one peer. Exactly two channel
// classes, because two is what the traffic actually divides into and a third
// would be a mechanism without a consumer:
//
//   - Unreliable-sequenced: newest wins, stale dropped, nothing resent.
//     Snapshots, inputs, and cues. A snapshot that arrives after a newer one is
//     worthless, so delivering it would be worse than dropping it.
//   - Reliable-ordered: windowed ack, resend, fragmentation. Handshake, table
//     sync, zone grants and baselines, cvar sync, disconnect. Anything a peer
//     must not miss is, by definition, state -- and state travels here.
//
// Scope is stated honestly: this is a small reliability layer for a handful of
// peers and kilobyte-scale control traffic, not a general congestion-controlled
// stream. A per-peer send budget stands in for congestion control at this
// scale.
//=============================================================================

enum class NetChannelKind : std::uint8_t
{
    UnreliableSequenced = 0,
    ReliableOrdered = 1,
};

// Space the AEAD will need, subtracted from the payload budget now so that
// turning on crypto does not shrink the largest message that fits. The nonce
// comes from the sequence space in the header rather than from bytes of its
// own, so only the tag is reserved here. Retrofitting this later would reshape
// every packet; reserving it costs 16 bytes today.
inline constexpr std::size_t kNetAeadTagBytes = 16;

// channel(1) + flags(1) + packetSequence(2) + ack(2) + ackBits(4)
inline constexpr std::size_t kNetPacketHeaderBytes = 10;

// Reliable packets carry a message id and fragment position on top of that.
// The packet sequence and the message id are deliberately different numbers:
// the sequence is what gets acked, one per datagram, and the id is what gets
// ordered, one per message. Ordering by the sequence instead would stall
// delivery the moment a message spanned more than one datagram, because the
// fragments consume sequences that never deliver anything of their own.
// messageId(2) + fragmentIndex(1) + fragmentCount(1)
inline constexpr std::size_t kNetReliableHeaderBytes = 4;

inline constexpr std::size_t kNetMaxPayloadBytes =
    kNetMaxDatagramBytes - kNetPacketHeaderBytes - kNetAeadTagBytes;
inline constexpr std::size_t kNetMaxFragmentPayloadBytes =
    kNetMaxPayloadBytes - kNetReliableHeaderBytes;

// A reliable message may span this many fragments. The cap bounds what a peer
// can make the receiver hold while a reassembly is incomplete.
inline constexpr std::uint8_t kNetMaxFragments = 32;
inline constexpr std::size_t kNetMaxReliableMessageBytes =
    kNetMaxFragmentPayloadBytes * kNetMaxFragments;

// The ack window: the newest reliable sequence a peer reports, plus a bitfield
// covering the 32 before it.
inline constexpr std::size_t kNetAckHistoryBits = 32;

// How many unacknowledged reliable datagrams may be outstanding before Send
// refuses. A peer that stops acking must not be able to grow this without
// bound; refusing is backpressure, and the session disconnects it.
//
// This must never exceed what the ack window can retire. A datagram further
// behind the newest than the history is deep can never be acknowledged, so it
// would resend until the session died while the receiver discarded every copy
// as an already-delivered duplicate -- a livelock that only appears once enough
// messages are in flight at once, which is exactly when it is hardest to
// diagnose.
inline constexpr std::size_t kNetReliableWindow = kNetAckHistoryBits;

// Incomplete reassemblies held at once. Each one a peer starts and never
// finishes costs memory until it is evicted.
inline constexpr std::size_t kNetMaxPendingReassemblies = 8;

//=============================================================================
// Sequence arithmetic
//
// Sequences are 16-bit and wrap. Comparison is by shortest signed distance, so
// 0 is correctly newer than 65535 -- the classic bug is comparing them as plain
// integers and treating a wrap as sixty-five thousand packets of reordering.
//=============================================================================
[[nodiscard]] bool NetSequenceGreaterThan(std::uint16_t lhs, std::uint16_t rhs);

//=============================================================================
// NetChannelSet
//
// Both channels for one peer, and the framing they share. Holds no transport
// and no address: the session decides where bytes go, this decides what they
// mean. That keeps it testable against a pair of buffers with no socket and no
// session in the way.
//=============================================================================
class NetChannelSet
{
public:
    struct Packet
    {
        std::vector<std::byte> Bytes;
    };

    // Queues a message. Unreliable is fire-and-forget; reliable is retained
    // until acked and fragmented if it does not fit one datagram.
    // False when the message is larger than the channel allows, or when the
    // reliable window is full.
    [[nodiscard]] bool Send(NetChannelKind kind, std::span<const std::byte> message);

    // Everything to put on the wire now, including resends whose timer has
    // expired. `nowSeconds` is passed rather than read so the whole layer is
    // deterministic under test.
    [[nodiscard]] std::vector<Packet> Drain(double nowSeconds);

    // Feeds one received datagram. Returns the messages it completed, in
    // delivery order: reliable ones in sequence order, unreliable ones only if
    // newer than the newest already seen. A malformed packet yields nothing and
    // reports the error, which the session counts as a strike.
    [[nodiscard]] std::vector<std::vector<std::byte>> Receive(
        std::span<const std::byte> packet, NetDecodeError& error);

    void SetResendSeconds(double seconds) { ResendSeconds = seconds; }

    [[nodiscard]] std::size_t OutstandingReliable() const { return Outstanding.size(); }
    [[nodiscard]] std::uint64_t Resends() const { return ResendCount; }
    [[nodiscard]] std::uint64_t StaleDropped() const { return StaleCount; }
    [[nodiscard]] std::uint64_t DuplicatesIgnored() const { return DuplicateCount; }

private:
    struct Outbound
    {
        std::uint16_t Sequence = 0;
        std::vector<std::byte> Packet;
        double LastSentSeconds = -1.0;
        bool EverSent = false;
    };

    struct Reassembly
    {
        std::uint16_t MessageId = 0;
        std::uint8_t Count = 0;
        std::uint8_t Received = 0;
        std::vector<std::vector<std::byte>> Parts;
        // Presence tracked separately from the bytes: a legitimately empty
        // fragment is indistinguishable from a missing one otherwise, and a
        // resend of one would count twice and complete the set early.
        std::vector<bool> Seen;
    };

    void AckOutbound(std::uint16_t ack, std::uint32_t ackBits);
    void RecordReliableReceipt(std::uint16_t sequence);
    [[nodiscard]] std::vector<std::byte> BuildPacket(NetChannelKind kind,
                                                     std::uint16_t packetSequence,
                                                     std::span<const std::byte> payload,
                                                     std::uint16_t messageId,
                                                     std::uint8_t fragmentIndex,
                                                     std::uint8_t fragmentCount) const;

    // Send state.
    std::uint16_t NextUnreliableSequence = 0;
    std::uint16_t NextReliableSequence = 0;
    std::uint16_t NextMessageId = 0;
    std::vector<Outbound> Outstanding;
    std::vector<Packet> PendingUnreliable;
    double ResendSeconds = 0.25;

    // Receive state.
    bool SeenUnreliable = false;
    std::uint16_t NewestUnreliable = 0;
    // The ack window this peer reports back: newest reliable sequence seen plus
    // the 32 before it, so one lost ack is covered by the next.
    bool SeenReliable = false;
    std::uint16_t NewestReliable = 0;
    std::uint32_t ReliableHistory = 0;
    // Ordered delivery, by message id rather than packet sequence. Starts at
    // zero because the first message a peer sends is message zero: deriving it
    // from whichever message arrives first would let a reordered or resent
    // later message define the start, and everything genuinely earlier would
    // then be discarded as already delivered.
    std::uint16_t NextReliableDelivery = 0;
    std::unordered_map<std::uint16_t, std::vector<std::byte>> Held;
    std::deque<Reassembly> Reassemblies;

    std::uint64_t ResendCount = 0;
    std::uint64_t StaleCount = 0;
    std::uint64_t DuplicateCount = 0;
};
