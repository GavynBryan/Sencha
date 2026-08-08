#include <net/NetCVarSync.h>

#include <core/console/ConsoleRegistry.h>
#include <net/NetProtocol.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <variant>

namespace
{
    constexpr std::size_t kKindBytes = 1;

    bool IsKnownType(std::uint8_t raw, CVarType& out)
    {
        switch (static_cast<CVarType>(raw))
        {
        case CVarType::Bool:
        case CVarType::Int:
        case CVarType::Double:
        case CVarType::String:
            out = static_cast<CVarType>(raw);
            return true;
        }
        return false;
    }

    // The value as text, used only to decide whether anything changed since the
    // last send. Comparing the rendered form keeps this one line per type
    // instead of a variant visit that has to stay in step with CVarType.
    std::string RenderedValue(const CVarMetadata& metadata)
    {
        return ToString(metadata.CurrentValue);
    }

    NetCVarUpdate UpdateFrom(const CVarMetadata& metadata)
    {
        NetCVarUpdate update;
        update.Name = metadata.Name;
        update.Type = metadata.Type;
        if (const bool* flag = std::get_if<bool>(&metadata.CurrentValue))
            update.BoolValue = *flag;
        else if (const std::int64_t* whole = std::get_if<std::int64_t>(&metadata.CurrentValue))
            update.IntValue = *whole;
        else if (const double* real = std::get_if<double>(&metadata.CurrentValue))
            update.DoubleValue = *real;
        else if (const std::string* text = std::get_if<std::string>(&metadata.CurrentValue))
            update.StringValue = *text;
        return update;
    }
}

std::size_t NetEncodeCVarUpdate(const NetCVarUpdate& update,
                                std::span<std::byte> out)
{
    if (update.Name.size() > kNetMaxCVarNameBytes
        || update.StringValue.size() > kNetMaxCVarStringBytes)
    {
        return 0;
    }

    NetWriter writer(out);
    writer.WriteU8(static_cast<std::uint8_t>(NetPayloadKind::CVar));
    writer.WriteString(update.Name, kNetMaxCVarNameBytes);
    writer.WriteU8(static_cast<std::uint8_t>(update.Type));

    switch (update.Type)
    {
    case CVarType::Bool:
        writer.WriteU8(update.BoolValue ? 1u : 0u);
        break;
    case CVarType::Int:
        writer.WriteU64(static_cast<std::uint64_t>(update.IntValue));
        break;
    case CVarType::Double:
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(double));
        std::memcpy(&bits, &update.DoubleValue, sizeof(bits));
        writer.WriteU64(bits);
        break;
    }
    case CVarType::String:
        writer.WriteString(update.StringValue, kNetMaxCVarStringBytes);
        break;
    }

    return writer.Overflowed() ? 0 : writer.Size();
}

bool NetDecodeCVarUpdate(std::span<const std::byte> bytes, NetCVarUpdate& out)
{
    if (bytes.size() < kKindBytes)
        return false;
    if (static_cast<NetPayloadKind>(bytes[0]) != NetPayloadKind::CVar)
        return false;

    NetReader reader(bytes.subspan(kKindBytes));
    if (!reader.ReadString(out.Name, kNetMaxCVarNameBytes))
        return false;

    std::uint8_t rawType = 0;
    if (!reader.ReadU8(rawType) || !IsKnownType(rawType, out.Type))
        return false;

    switch (out.Type)
    {
    case CVarType::Bool:
    {
        std::uint8_t value = 0;
        if (!reader.ReadU8(value))
            return false;
        out.BoolValue = value != 0;
        break;
    }
    case CVarType::Int:
    {
        std::uint64_t value = 0;
        if (!reader.ReadU64(value))
            return false;
        out.IntValue = static_cast<std::int64_t>(value);
        break;
    }
    case CVarType::Double:
    {
        std::uint64_t bits = 0;
        if (!reader.ReadU64(bits))
            return false;
        std::memcpy(&out.DoubleValue, &bits, sizeof(out.DoubleValue));
        break;
    }
    case CVarType::String:
        if (!reader.ReadString(out.StringValue, kNetMaxCVarStringBytes))
            return false;
        break;
    }

    // Trailing bytes are an error here for the same reason they are in every
    // other decoder: a decoder that ignores them lets a peer smuggle content
    // past a reviewer's reading of the format.
    return reader.AtEnd();
}

NetCVarPublisher::PublishStats NetCVarPublisher::Publish(
    NetSession& session, const ConsoleRegistry& registry)
{
    PublishStats stats;
    if (session.Role() != NetSessionRole::Host)
        return stats;

    const std::vector<PeerId> peers = session.ConnectedPeers();
    if (peers.empty())
        return stats;

    std::array<std::byte, 512> scratch{};

    for (PeerId peer : peers)
    {
        auto& seen = Sent[peer.Value];
        // Hidden included on purpose: whether a value is shown in a listing has
        // nothing to do with whether the session owns it.
        for (const CVarMetadata* metadata : registry.ListCVars({}, true))
        {
            if (!HasFlag(metadata->Flags, CVarFlags::Replicated))
                continue;

            const std::string rendered = RenderedValue(*metadata);
            const auto known = seen.find(metadata->Name);
            if (known != seen.end() && known->second == rendered)
                continue;

            const std::size_t size = NetEncodeCVarUpdate(UpdateFrom(*metadata), scratch);
            if (size == 0)
                continue;
            if (!session.Send(peer, NetChannelKind::ReliableOrdered,
                              std::span<const std::byte>(scratch).subspan(0, size)))
            {
                // Backpressure: the peer is not keeping up. Deliberately not
                // recorded as sent, so it goes again next time rather than
                // being lost.
                continue;
            }

            seen[metadata->Name] = rendered;
            ++stats.Updates;
            stats.BytesQueued += size;
        }
    }

    // Peers that left stop costing a record.
    std::erase_if(Sent, [&peers](const auto& entry) {
        return std::find_if(peers.begin(), peers.end(), [&entry](PeerId peer) {
                   return peer.Value == entry.first;
               }) == peers.end();
    });

    return stats;
}

void NetCVarPublisher::ForgetPeer(PeerId peer)
{
    Sent.erase(peer.Value);
}

bool NetApplyCVarUpdate(ConsoleRegistry& registry, const NetCVarUpdate& update)
{
    CVarMetadata* metadata = registry.FindCVar(update.Name);
    // A name this build does not have, or one the session does not own, is
    // refused rather than created. A client's console is its own; an authority
    // does not get to add to it or to reach past the flag that says which
    // values are the session's.
    if (metadata == nullptr || !HasFlag(metadata->Flags, CVarFlags::Replicated))
        return false;
    if (metadata->Type != update.Type)
        return false;

    CVarValue value;
    switch (update.Type)
    {
    case CVarType::Bool:   value = update.BoolValue; break;
    case CVarType::Int:    value = update.IntValue; break;
    case CVarType::Double: value = update.DoubleValue; break;
    case CVarType::String: value = update.StringValue; break;
    }

    // Forced: this is the authority's decision arriving, not a request from
    // whoever is sitting at this machine, so it passes the same rules that
    // would refuse them. Range and enum validation still applies inside.
    const ConsoleResult result = registry.SetCVar(
        update.Name, value, ConsoleValueSource{ "server" },
        ConsolePhase::EngineReady, true);
    return result.Succeeded() || result.Status == ConsoleStatus::Deferred;
}
