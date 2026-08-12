#include <net/NetMessageRouter.h>

#include <algorithm>
#include <vector>

namespace
{
    bool InGameRange(std::uint8_t kind)
    {
        return kind >= kNetFirstGamePayloadKind;
    }

    std::size_t SlotOf(std::uint8_t kind)
    {
        return static_cast<std::size_t>(kind - kNetFirstGamePayloadKind);
    }

    // The kind byte and the body, in one buffer, because the channel takes one
    // span. Reused per call rather than per send: this is not a per-tick path.
    std::size_t SendStamped(NetSession& session, PeerId to, bool broadcast,
                            NetChannelKind channel, std::uint8_t kind,
                            std::span<const std::byte> body, NetStats* traffic)
    {
        if (!InGameRange(kind))
            return 0;
        if (body.size() + kNetPayloadKindBytes > kNetMaxPayloadBytes)
            return 0;

        std::vector<std::byte> payload;
        payload.reserve(kNetPayloadKindBytes + body.size());
        payload.push_back(static_cast<std::byte>(kind));
        payload.insert(payload.end(), body.begin(), body.end());

        if (broadcast)
        {
            session.Broadcast(channel, payload);
        }
        else if (!session.Send(to, channel, payload))
        {
            return 0;
        }

        // Counted here because nothing else will: the frame phase records the
        // three kinds it sends itself, and a game's traffic used to leave no
        // trace in the numbers anyone reads to ask what grew.
        if (traffic != nullptr)
            traffic->RecordOut(NetTrafficKind::Game, payload.size());
        return payload.size();
    }
}

bool NetMessageRouter::Bind(std::uint8_t kind, NetMessageDirection direction,
                            NetMessageHandler handler, void* context)
{
    if (!InGameRange(kind) || handler == nullptr)
        return false;

    Entry& entry = Entries[SlotOf(kind)];
    if (entry.Handler != nullptr)
        return false;

    entry.Handler = handler;
    entry.Context = context;
    entry.Direction = direction;
    return true;
}

bool NetMessageRouter::Route(NetSessionRole role, std::uint8_t kind,
                             const NetMessageContext& message) const
{
    if (!InGameRange(kind))
        return false;

    const Entry& entry = Entries[SlotOf(kind)];
    if (entry.Handler == nullptr)
        return false;

    // Direction, before the handler is reached. A kind that only travels one
    // way arriving the other way is a different build or something probing the
    // port, and recognising that is not a handler's job.
    const bool wantsAuthority = entry.Direction == NetMessageDirection::ClientToAuthority;
    if (wantsAuthority != (role == NetSessionRole::Host))
        return false;

    return entry.Handler(entry.Context, message);
}

bool NetMessageRouter::IsBound(std::uint8_t kind) const
{
    return InGameRange(kind) && Entries[SlotOf(kind)].Handler != nullptr;
}

std::size_t NetMessageRouter::BoundKinds() const
{
    return static_cast<std::size_t>(std::count_if(
        Entries.begin(), Entries.end(),
        [](const Entry& entry) { return entry.Handler != nullptr; }));
}

void NetMessageRouter::Clear()
{
    Entries = {};
}

std::size_t NetSendToAuthority(NetSession& session, NetChannelKind channel,
                               std::uint8_t kind, std::span<const std::byte> body,
                               NetStats* traffic)
{
    if (session.Role() != NetSessionRole::Client)
        return 0;
    // A client's channel goes to the one place it is connected to; the peer
    // argument is a placeholder the session ignores, which is exactly why this
    // is its own function rather than a call site passing its own id.
    return SendStamped(session, session.LocalPeerId(), false, channel, kind, body,
                       traffic);
}

std::size_t NetSendToPeer(NetSession& session, PeerId to, NetChannelKind channel,
                          std::uint8_t kind, std::span<const std::byte> body,
                          NetStats* traffic)
{
    if (session.Role() != NetSessionRole::Host || !to.IsValid())
        return 0;
    return SendStamped(session, to, false, channel, kind, body, traffic);
}

std::size_t NetBroadcastToPeers(NetSession& session, NetChannelKind channel,
                                std::uint8_t kind, std::span<const std::byte> body,
                                NetStats* traffic)
{
    if (session.Role() != NetSessionRole::Host)
        return 0;
    return SendStamped(session, PeerId{}, true, channel, kind, body, traffic);
}
