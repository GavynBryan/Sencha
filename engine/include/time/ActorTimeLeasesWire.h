#pragma once

#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>
#include <time/ActorTimeLeases.h>

#include <cstdint>

// The owner's wire entity, the owner's key, ScaleQ16, FromTick, UntilTick.
struct ActorTimeLeaseWire
{
    std::uint64_t Values[5] = {};
};

// ActiveCount, HistoryCount, Evicted.
struct ActorTimeLeaseWireCounts
{
    std::uint32_t Values[3] = {};
};

static_assert(kActorTimeLeaseCapacity == 8 && kActorTimeHistoryCapacity == 16,
              "ActorTimeLeasesWire names one member per lease.");

// An actor's leases go to every machine that sees it: they are the scale it runs at
// on any tick, which a remote machine animates by.
struct ActorTimeLeasesWire
{
    ActorTimeLeaseWire A0, A1, A2, A3, A4, A5, A6, A7;
    ActorTimeLeaseWire H0, H1, H2, H3, H4, H5, H6, H7, H8, H9, H10, H11, H12, H13, H14, H15;
    ActorTimeLeaseWireCounts Counts;
};

template <>
struct PackedScalarGroup<ActorTimeLeaseWire>
{
    static constexpr std::size_t Count = 5;
    using Component = std::uint64_t;
};

template <>
struct PackedScalarGroup<ActorTimeLeaseWireCounts>
{
    static constexpr std::size_t Count = 3;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<ActorTimeLeasesWire>
{
    static constexpr std::string_view Name = "actor_time_leases_wire";
    static auto Fields()
    {
        using W = ActorTimeLeasesWire;
        return std::tuple{
            MakeField("a0", &W::A0), MakeField("a1", &W::A1), MakeField("a2", &W::A2), MakeField("a3", &W::A3),
            MakeField("a4", &W::A4), MakeField("a5", &W::A5), MakeField("a6", &W::A6), MakeField("a7", &W::A7),
            MakeField("h0", &W::H0), MakeField("h1", &W::H1), MakeField("h2", &W::H2), MakeField("h3", &W::H3),
            MakeField("h4", &W::H4), MakeField("h5", &W::H5), MakeField("h6", &W::H6), MakeField("h7", &W::H7),
            MakeField("h8", &W::H8), MakeField("h9", &W::H9), MakeField("h10", &W::H10), MakeField("h11", &W::H11),
            MakeField("h12", &W::H12), MakeField("h13", &W::H13), MakeField("h14", &W::H14),
            MakeField("h15", &W::H15), MakeField("counts", &W::Counts) };
    }
};

template <>
struct ReplicationCodec<ActorTimeLeases>
{
    using Wire = ActorTimeLeasesWire;
    static void ToWire(const ReplicationWireContext& context, const ActorTimeLeases& leases, ActorTimeLeasesWire& wire);
    static void FromWire(const ReplicationWireContext& context, const ActorTimeLeasesWire& wire, ActorTimeLeases& leases);
};
