#include <time/ActorTimeLeasesWire.h>

#include <algorithm>
#include <array>

namespace
{
constexpr std::size_t kLeases = kActorTimeLeaseCapacity + kActorTimeHistoryCapacity;

std::array<ActorTimeLeaseWire*, kLeases> LeasesOf(ActorTimeLeasesWire& w)
{
    return { &w.A0, &w.A1, &w.A2,  &w.A3,  &w.A4,  &w.A5,  &w.A6,  &w.A7,  &w.H0,  &w.H1,  &w.H2,  &w.H3,
             &w.H4, &w.H5, &w.H6,  &w.H7,  &w.H8,  &w.H9,  &w.H10, &w.H11, &w.H12, &w.H13, &w.H14, &w.H15 };
}

std::array<const ActorTimeLeaseWire*, kLeases> LeasesOf(const ActorTimeLeasesWire& w)
{
    return { &w.A0, &w.A1, &w.A2,  &w.A3,  &w.A4,  &w.A5,  &w.A6,  &w.A7,  &w.H0,  &w.H1,  &w.H2,  &w.H3,
             &w.H4, &w.H5, &w.H6,  &w.H7,  &w.H8,  &w.H9,  &w.H10, &w.H11, &w.H12, &w.H13, &w.H14, &w.H15 };
}

ActorTimeLease* LeaseAt(ActorTimeLeases& leases, std::size_t i)
{
    return i < kActorTimeLeaseCapacity ? &leases.Active[i] : &leases.History[i - kActorTimeLeaseCapacity];
}

const ActorTimeLease* LeaseAt(const ActorTimeLeases& leases, std::size_t i)
{
    return i < kActorTimeLeaseCapacity ? &leases.Active[i] : &leases.History[i - kActorTimeLeaseCapacity];
}

bool Held(const ActorTimeLeases& leases, std::size_t i)
{
    return i < kActorTimeLeaseCapacity ? i < leases.ActiveCount : i - kActorTimeLeaseCapacity < leases.HistoryCount;
}
}

void ReplicationCodec<ActorTimeLeases>::ToWire(const ReplicationWireContext& context, const ActorTimeLeases& leases,
                                               ActorTimeLeasesWire& wire)
{
    wire = ActorTimeLeasesWire{};
    const auto out = LeasesOf(wire);
    for (std::size_t i = 0; i < kLeases; ++i)
    {
        if (!Held(leases, i))
            continue;
        const ActorTimeLease& lease = *LeaseAt(leases, i);
        out[i]->Values[0] = context.WireEntity(lease.Owner.Entity);
        out[i]->Values[1] = lease.Owner.Key;
        out[i]->Values[2] = static_cast<std::uint64_t>(lease.ScaleQ16);
        out[i]->Values[3] = lease.FromTick;
        out[i]->Values[4] = lease.UntilTick;
    }
    wire.Counts.Values[0] = leases.ActiveCount;
    wire.Counts.Values[1] = leases.HistoryCount;
    wire.Counts.Values[2] = leases.Evicted;
}

void ReplicationCodec<ActorTimeLeases>::FromWire(const ReplicationWireContext& context,
                                                 const ActorTimeLeasesWire& wire, ActorTimeLeases& leases)
{
    leases = ActorTimeLeases{};
    leases.ActiveCount = static_cast<std::uint8_t>(std::min<std::uint32_t>(wire.Counts.Values[0], kActorTimeLeaseCapacity));
    leases.HistoryCount =
        static_cast<std::uint8_t>(std::min<std::uint32_t>(wire.Counts.Values[1], kActorTimeHistoryCapacity));
    leases.Evicted = wire.Counts.Values[2];
    const auto in = LeasesOf(wire);
    for (std::size_t i = 0; i < kLeases; ++i)
    {
        if (!Held(leases, i))
            continue;
        ActorTimeLease& lease = *LeaseAt(leases, i);
        lease.Owner.Entity = context.LocalEntity(in[i]->Values[0]);
        lease.Owner.Key = in[i]->Values[1];
        lease.ScaleQ16 = static_cast<std::int64_t>(in[i]->Values[2]);
        lease.FromTick = in[i]->Values[3];
        lease.UntilTick = in[i]->Values[4];
    }
}
