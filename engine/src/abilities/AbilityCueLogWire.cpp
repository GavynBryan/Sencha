#include <abilities/AbilityCueLogWire.h>

#include <gameplay_tags/GameplayTagWire.h>

#include <array>

namespace
{
std::array<AbilityCueWireEntry*, kAbilityCueLogCapacity> EntriesOf(AbilityCueLogWire& wire)
{
    return { &wire.E0, &wire.E1, &wire.E2, &wire.E3, &wire.E4, &wire.E5, &wire.E6, &wire.E7 };
}

std::array<const AbilityCueWireEntry*, kAbilityCueLogCapacity> EntriesOf(const AbilityCueLogWire& wire)
{
    return { &wire.E0, &wire.E1, &wire.E2, &wire.E3, &wire.E4, &wire.E5, &wire.E6, &wire.E7 };
}
}

void ReplicationCodec<AbilityCueLog>::ToWire(const ReplicationWireContext& context, const AbilityCueLog& log,
                                             AbilityCueLogWire& wire)
{
    const auto entries = EntriesOf(wire);
    for (std::size_t i = 0; i < kAbilityCueLogCapacity; ++i)
    {
        AbilityCueWireEntry& out = *entries[i];
        out = AbilityCueWireEntry{};
        const AbilityCueEntry& entry = log.Entries[i];
        if (entry.Seq == 0)
            continue;
        out.Ticks.Values[0] = entry.Activation;
        out.Ticks.Values[1] = entry.Tick;
        out.Ticks.Values[2] = context.WireEntity(entry.Target);
        out.Words.Values[0] = static_cast<std::uint32_t>(entry.Seq) | static_cast<std::uint32_t>(entry.Op) << 16;
        out.Words.Values[1] = WireTag(context, entry.Cue);
        out.Words.Values[2] = entry.Ordinal;
        out.Point = entry.Point;
        out.Normal = entry.Normal;
    }
    wire.Counters.Values[0] = log.NextSeq;
    wire.Counters.Values[1] = log.Overwritten;
}

void ReplicationCodec<AbilityCueLog>::FromWire(const ReplicationWireContext& context, const AbilityCueLogWire& wire,
                                               AbilityCueLog& log)
{
    const auto entries = EntriesOf(wire);
    for (std::size_t i = 0; i < kAbilityCueLogCapacity; ++i)
    {
        const AbilityCueWireEntry& in = *entries[i];
        AbilityCueEntry& entry = log.Entries[i];
        entry = AbilityCueEntry{};
        entry.Seq = static_cast<std::uint16_t>(in.Words.Values[0]);
        if (entry.Seq == 0)
            continue;
        entry.Op = static_cast<std::uint16_t>(in.Words.Values[0] >> 16);
        entry.Cue = LocalTag(context, in.Words.Values[1]);
        entry.Ordinal = static_cast<std::uint8_t>(in.Words.Values[2]);
        entry.Activation = in.Ticks.Values[0];
        entry.Tick = in.Ticks.Values[1];
        entry.Target = context.LocalEntity(in.Ticks.Values[2]);
        entry.Point = in.Point;
        entry.Normal = in.Normal;
    }
    log.NextSeq = static_cast<std::uint16_t>(wire.Counters.Values[0]);
    log.Overwritten = wire.Counters.Values[1];
}
