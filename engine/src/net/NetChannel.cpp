#include <net/NetChannel.h>

#include <algorithm>
#include <utility>

namespace
{
    // Shortest signed distance around the 16-bit circle. Comparing sequences as
    // plain integers reads a wrap as sixty-five thousand packets of reordering
    // and throws away everything after it.
    constexpr std::uint16_t kSequenceHalf = 32768;

    // Where the ack window sits in every packet, so a resend can refresh it in
    // place rather than rebuilding the packet around unchanged payload bytes.
    constexpr std::size_t kAckOffset = 4;
    constexpr std::size_t kAckBitsOffset = 6;
}

static_assert(kNetReliableWindow <= kNetAckHistoryBits + 1,
              "the reliable send window must fit the ack window: a datagram the "
              "receiver cannot acknowledge resends forever");

bool NetSequenceGreaterThan(std::uint16_t lhs, std::uint16_t rhs)
{
    return ((lhs > rhs) && (lhs - rhs <= kSequenceHalf))
        || ((lhs < rhs) && (rhs - lhs > kSequenceHalf));
}

std::vector<std::byte> NetChannelSet::BuildPacket(NetChannelKind kind,
                                                  std::uint16_t packetSequence,
                                                  std::span<const std::byte> payload,
                                                  std::uint16_t messageId,
                                                  std::uint8_t fragmentIndex,
                                                  std::uint8_t fragmentCount) const
{
    std::vector<std::byte> packet;
    packet.reserve(kNetPacketHeaderBytes + kNetReliableHeaderBytes + payload.size());

    const auto push16 = [&packet](std::uint16_t value) {
        packet.push_back(static_cast<std::byte>(value & 0xFF));
        packet.push_back(static_cast<std::byte>(value >> 8));
    };

    packet.push_back(static_cast<std::byte>(kind));
    packet.push_back(std::byte{ 0 });  // flags, reserved
    push16(packetSequence);
    // Every packet carries the ack window, whichever channel it belongs to, so
    // a steady stream of unreliable traffic acknowledges reliable messages
    // without the reliable channel having to send anything of its own.
    push16(NewestReliable);
    for (int shift = 0; shift < 32; shift += 8)
        packet.push_back(static_cast<std::byte>((ReliableHistory >> shift) & 0xFF));

    if (kind == NetChannelKind::ReliableOrdered)
    {
        push16(messageId);
        packet.push_back(static_cast<std::byte>(fragmentIndex));
        packet.push_back(static_cast<std::byte>(fragmentCount));
    }

    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

bool NetChannelSet::Send(NetChannelKind kind, std::span<const std::byte> message)
{
    if (kind == NetChannelKind::UnreliableSequenced)
    {
        // Never fragmented: a message worth dropping when it is stale is not
        // worth reassembling, and a partial snapshot is not a snapshot.
        if (message.size() > kNetMaxPayloadBytes)
            return false;

        const std::uint16_t sequence = NextUnreliableSequence++;
        PendingUnreliable.push_back(Packet{
            BuildPacket(kind, sequence, message, 0, 0, 1) });
        return true;
    }

    if (message.size() > kNetMaxReliableMessageBytes)
        return false;

    const std::size_t fragmentCount =
        message.empty()
            ? 1
            : (message.size() + kNetMaxFragmentPayloadBytes - 1) / kNetMaxFragmentPayloadBytes;

    // The window bounds datagrams in flight, so a fragmented message counts for
    // all of its fragments rather than slipping through as one.
    if (Outstanding.size() + fragmentCount > kNetReliableWindow)
        return false;

    // And it bounds the sequence SPAN, not just the count. Retiring an old
    // datagram frees a slot, but sending a new one advances the sequence the
    // peer reports as newest; do that often enough and the oldest datagram
    // still outstanding falls further behind that newest than the ack history
    // is deep, at which point nothing can ever retire it. It then resends for
    // the life of the session while the receiver discards every copy. Bounding
    // the count alone does not prevent this: a steady stream of new messages
    // walks the window right off the back of the one straggler.
    if (!Outstanding.empty())
    {
        const std::uint16_t oldest = Outstanding.front().Sequence;
        const std::uint16_t newest =
            static_cast<std::uint16_t>(NextReliableSequence + fragmentCount - 1);
        if (static_cast<std::uint16_t>(newest - oldest) >= kNetAckHistoryBits)
            return false;
    }

    const std::uint16_t messageId = NextMessageId++;
    for (std::size_t index = 0; index < fragmentCount; ++index)
    {
        const std::size_t offset = index * kNetMaxFragmentPayloadBytes;
        const std::size_t length =
            message.empty()
                ? 0
                : std::min(kNetMaxFragmentPayloadBytes, message.size() - offset);
        const std::uint16_t sequence = NextReliableSequence++;
        Outstanding.push_back(Outbound{
            .Sequence = sequence,
            .Packet = BuildPacket(kind,
                                  sequence,
                                  message.subspan(offset, length),
                                  messageId,
                                  static_cast<std::uint8_t>(index),
                                  static_cast<std::uint8_t>(fragmentCount)),
        });
    }
    return true;
}

std::vector<NetChannelSet::Packet> NetChannelSet::Drain(double nowSeconds)
{
    std::vector<Packet> out;
    out.swap(PendingUnreliable);

    for (Outbound& outbound : Outstanding)
    {
        const bool due =
            !outbound.EverSent
            || (nowSeconds - outbound.LastSentSeconds) >= ResendSeconds;
        if (!due)
            continue;

        if (outbound.EverSent)
            ++ResendCount;

        // The ack window is refreshed in place before every send: a resend that
        // repeated a stale ack would tell the peer nothing it did not already
        // know, and the ack is the only thing in the packet that has aged.
        std::vector<std::byte>& packet = outbound.Packet;
        packet[kAckOffset] = static_cast<std::byte>(NewestReliable & 0xFF);
        packet[kAckOffset + 1] = static_cast<std::byte>(NewestReliable >> 8);
        for (int shift = 0; shift < 32; shift += 8)
        {
            packet[kAckBitsOffset + shift / 8] =
                static_cast<std::byte>((ReliableHistory >> shift) & 0xFF);
        }

        out.push_back(Packet{ packet });
        outbound.EverSent = true;
        outbound.LastSentSeconds = nowSeconds;
    }

    return out;
}

void NetChannelSet::AckOutbound(std::uint16_t ack, std::uint32_t ackBits)
{
    const auto acked = [&](std::uint16_t sequence) {
        if (sequence == ack)
            return true;
        if (NetSequenceGreaterThan(sequence, ack))
            return false;
        const std::uint16_t distance = static_cast<std::uint16_t>(ack - sequence);
        if (distance == 0 || distance > kNetAckHistoryBits)
            return false;
        return (ackBits & (1u << (distance - 1))) != 0;
    };

    std::erase_if(Outstanding,
                  [&](const Outbound& outbound) { return acked(outbound.Sequence); });
}

void NetChannelSet::RecordReliableReceipt(std::uint16_t sequence)
{
    if (!SeenReliable)
    {
        SeenReliable = true;
        NewestReliable = sequence;
        ReliableHistory = 0;
        return;
    }

    if (NetSequenceGreaterThan(sequence, NewestReliable))
    {
        const std::uint16_t shift = static_cast<std::uint16_t>(sequence - NewestReliable);
        // The old newest becomes a history bit; anything shifted past 32 falls
        // off, which is what bounds the window.
        if (shift >= kNetAckHistoryBits)
            ReliableHistory = 0;
        else
            ReliableHistory = (ReliableHistory << shift) | (1u << (shift - 1));
        NewestReliable = sequence;
        return;
    }

    const std::uint16_t distance = static_cast<std::uint16_t>(NewestReliable - sequence);
    if (distance > 0 && distance <= kNetAckHistoryBits)
        ReliableHistory |= (1u << (distance - 1));
}

std::vector<std::vector<std::byte>> NetChannelSet::Receive(
    std::span<const std::byte> packet, NetDecodeError& error)
{
    error = NetDecodeError::None;
    std::vector<std::vector<std::byte>> delivered;

    if (packet.size() < kNetPacketHeaderBytes)
    {
        error = NetDecodeError::Truncated;
        return delivered;
    }

    const auto read16 = [&packet](std::size_t at) {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(packet[at])
            | (static_cast<std::uint16_t>(packet[at + 1]) << 8));
    };

    const auto kindRaw = static_cast<std::uint8_t>(packet[0]);
    if (kindRaw > static_cast<std::uint8_t>(NetChannelKind::ReliableOrdered))
    {
        error = NetDecodeError::UnknownMessage;
        return delivered;
    }
    const auto kind = static_cast<NetChannelKind>(kindRaw);
    const std::uint16_t sequence = read16(2);
    const std::uint16_t ack = read16(kAckOffset);
    std::uint32_t ackBits = 0;
    for (int shift = 0; shift < 32; shift += 8)
        ackBits |= static_cast<std::uint32_t>(packet[kAckBitsOffset + shift / 8]) << shift;

    // Acks ride every packet, so this runs before anything channel-specific.
    AckOutbound(ack, ackBits);

    if (kind == NetChannelKind::UnreliableSequenced)
    {
        if (SeenUnreliable && !NetSequenceGreaterThan(sequence, NewestUnreliable))
        {
            ++StaleCount;
            return delivered;
        }
        SeenUnreliable = true;
        NewestUnreliable = sequence;
        const std::span<const std::byte> payload = packet.subspan(kNetPacketHeaderBytes);
        delivered.emplace_back(payload.begin(), payload.end());
        return delivered;
    }

    if (packet.size() < kNetPacketHeaderBytes + kNetReliableHeaderBytes)
    {
        error = NetDecodeError::Truncated;
        return delivered;
    }

    const std::uint16_t messageId = read16(kNetPacketHeaderBytes);
    const auto fragmentIndex =
        static_cast<std::uint8_t>(packet[kNetPacketHeaderBytes + 2]);
    const auto fragmentCount =
        static_cast<std::uint8_t>(packet[kNetPacketHeaderBytes + 3]);

    // Checked before either value sizes or indexes anything.
    if (fragmentCount == 0 || fragmentCount > kNetMaxFragments
        || fragmentIndex >= fragmentCount)
    {
        error = NetDecodeError::Malformed;
        return delivered;
    }

    RecordReliableReceipt(sequence);

    const std::span<const std::byte> payload =
        packet.subspan(kNetPacketHeaderBytes + kNetReliableHeaderBytes);

    // Ordering is by message id, so a duplicate of an already-delivered message
    // is discarded before any reassembly state is touched for it.
    if (NetSequenceGreaterThan(NextReliableDelivery, messageId))
    {
        ++DuplicateCount;
        return delivered;
    }

    std::vector<std::byte> message;
    if (fragmentCount > 1)
    {
        Reassembly* found = nullptr;
        for (Reassembly& candidate : Reassemblies)
        {
            if (candidate.MessageId == messageId)
            {
                found = &candidate;
                break;
            }
        }
        if (found != nullptr && found->Count != fragmentCount)
        {
            // The same id arriving with a different fragment count is a peer
            // contradicting itself; the partial state is not safe to extend.
            error = NetDecodeError::Malformed;
            return delivered;
        }
        if (found == nullptr)
        {
            // Oldest out first. What a peer can make the receiver hold while a
            // reassembly never completes is bounded here, not by the peer.
            if (Reassemblies.size() >= kNetMaxPendingReassemblies)
                Reassemblies.pop_front();
            Reassemblies.push_back(Reassembly{
                .MessageId = messageId,
                .Count = fragmentCount,
                .Received = 0,
                .Parts = std::vector<std::vector<std::byte>>(fragmentCount),
                .Seen = std::vector<bool>(fragmentCount, false),
            });
            found = &Reassemblies.back();
        }

        if (!found->Seen[fragmentIndex])
        {
            found->Seen[fragmentIndex] = true;
            ++found->Received;
            found->Parts[fragmentIndex].assign(payload.begin(), payload.end());
        }

        if (found->Received < found->Count)
            return delivered;

        for (const std::vector<std::byte>& part : found->Parts)
            message.insert(message.end(), part.begin(), part.end());

        std::erase_if(Reassemblies, [messageId](const Reassembly& candidate) {
            return candidate.MessageId == messageId;
        });
    }
    else
    {
        message.assign(payload.begin(), payload.end());
    }

    if (messageId != NextReliableDelivery)
    {
        // Arrived early. Held until the gap fills, because ordered means
        // ordered: releasing it now would hand the session a later message
        // before an earlier one it may depend on.
        if (!Held.contains(messageId))
            Held.emplace(messageId, std::move(message));
        else
            ++DuplicateCount;
        return delivered;
    }

    delivered.push_back(std::move(message));
    ++NextReliableDelivery;

    // Releasing this one may have completed the run behind it.
    for (auto it = Held.find(NextReliableDelivery); it != Held.end();
         it = Held.find(NextReliableDelivery))
    {
        delivered.push_back(std::move(it->second));
        Held.erase(it);
        ++NextReliableDelivery;
    }

    return delivered;
}
