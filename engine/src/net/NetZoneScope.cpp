#include <net/NetZoneScope.h>

#include <net/NetProtocol.h>

#include <algorithm>

namespace
{
    bool IsKnownVerb(std::uint8_t raw, NetZoneScopeVerb& out)
    {
        switch (static_cast<NetZoneScopeVerb>(raw))
        {
        case NetZoneScopeVerb::Grant:
        case NetZoneScopeVerb::Revoke:
            out = static_cast<NetZoneScopeVerb>(raw);
            return true;
        }
        return false;
    }
}

std::size_t NetEncodeZoneScopeUpdate(const NetZoneScopeUpdate& update,
                                     std::span<std::byte> out)
{
    // Zone zero is the invalid id, and there is no such room to grant or
    // revoke. Refused at the encoder so it can never be a decoder's problem.
    if (!update.Zone.IsValid())
        return 0;

    NetWriter writer(out);
    writer.WriteU8(static_cast<std::uint8_t>(NetPayloadKind::ZoneScope));
    writer.WriteU8(static_cast<std::uint8_t>(update.Verb));
    writer.WriteU64(update.Zone.Value);
    return writer.Overflowed() ? 0 : writer.Size();
}

bool NetDecodeZoneScopeUpdate(std::span<const std::byte> bytes,
                              NetZoneScopeUpdate& out)
{
    if (bytes.size() < kNetPayloadKindBytes)
        return false;
    if (static_cast<NetPayloadKind>(bytes[0]) != NetPayloadKind::ZoneScope)
        return false;

    NetReader reader(bytes.subspan(kNetPayloadKindBytes));
    std::uint8_t rawVerb = 0;
    if (!reader.ReadU8(rawVerb) || !IsKnownVerb(rawVerb, out.Verb))
        return false;

    std::uint64_t zone = 0;
    if (!reader.ReadU64(zone) || zone == 0)
        return false;
    out.Zone = ZoneId{ zone };

    // Trailing bytes are an error here for the same reason they are in every
    // other decoder: one that ignores them lets a peer smuggle content past a
    // reviewer's reading of the format.
    return reader.AtEnd();
}

std::size_t NetEncodeZoneAck(ZoneId zone, std::span<std::byte> out)
{
    if (!zone.IsValid())
        return 0;

    NetWriter writer(out);
    writer.WriteU8(static_cast<std::uint8_t>(NetPayloadKind::ZoneAck));
    writer.WriteU64(zone.Value);
    return writer.Overflowed() ? 0 : writer.Size();
}

bool NetDecodeZoneAck(std::span<const std::byte> bytes, ZoneId& out)
{
    if (bytes.size() < kNetPayloadKindBytes)
        return false;
    if (static_cast<NetPayloadKind>(bytes[0]) != NetPayloadKind::ZoneAck)
        return false;

    NetReader reader(bytes.subspan(kNetPayloadKindBytes));
    std::uint64_t zone = 0;
    if (!reader.ReadU64(zone) || zone == 0)
        return false;
    out = ZoneId{ zone };
    return reader.AtEnd();
}

//=============================================================================
// The per-peer state
//=============================================================================

NetZoneScope::Entry* NetZoneScope::Find(ZoneId zone)
{
    const auto at = std::lower_bound(
        Zones_.begin(), Zones_.end(), zone,
        [](const Entry& held, ZoneId id) { return held.Zone.Value < id.Value; });
    return at != Zones_.end() && at->Zone == zone ? &*at : nullptr;
}

const NetZoneScope::Entry* NetZoneScope::Find(ZoneId zone) const
{
    return const_cast<NetZoneScope*>(this)->Find(zone);
}

bool NetZoneScope::Grant(ZoneId zone)
{
    if (!zone.IsValid())
        return false;

    const auto at = std::lower_bound(
        Zones_.begin(), Zones_.end(), zone,
        [](const Entry& held, ZoneId id) { return held.Zone.Value < id.Value; });
    if (at != Zones_.end() && at->Zone == zone)
        return false;  // Already loading it, or already holding it.

    Zones_.insert(at, Entry{ .Zone = zone, .State = NetZoneScopeState::Granted });
    return true;
}

bool NetZoneScope::Revoke(ZoneId zone)
{
    Entry* held = Find(zone);
    if (held == nullptr)
        return false;

    // Dropped rather than marked, and dropped now rather than when the peer
    // answers. The authority must stop sending state for the zone the moment it
    // decides the peer should let it go; waiting for a confirmation would leave
    // a window where it is still filling a room it has just closed. There is
    // nothing to confirm anyway -- unloading cannot fail.
    Zones_.erase(Zones_.begin() + (held - Zones_.data()));
    return true;
}

bool NetZoneScope::Acknowledge(ZoneId zone)
{
    Entry* held = Find(zone);
    if (held == nullptr)
        return false;
    held->State = NetZoneScopeState::Acked;
    return true;
}

NetZoneScopeState NetZoneScope::StateOf(ZoneId zone) const
{
    const Entry* held = Find(zone);
    return held == nullptr ? NetZoneScopeState::None : held->State;
}

bool NetZoneScope::CanReceive(ZoneId zone) const
{
    if (!zone.IsValid())
        return true;
    return StateOf(zone) == NetZoneScopeState::Acked;
}
