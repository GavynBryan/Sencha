#include <net/NetGrantedResidency.h>

#include <zone/WorldPartitionRuntime.h>

#include <algorithm>

void NetGrantedResidency::Update(const NetZoneScope& scope,
                                 WorldPartitionRuntime& partition,
                                 ZoneParticipation minimum)
{
    Wanted_.clear();
    for (const NetZoneScope::Entry& held : scope.Entries())
        Wanted_.push_back(held.Zone);

    // Granted as well as acked. The grant is what tells this machine to start
    // loading; waiting until it had acked would be waiting for the load this is
    // meant to cause.
    for (const ZoneId zone : Wanted_)
    {
        if (!std::binary_search(Pinned_.begin(), Pinned_.end(), zone,
                                [](ZoneId a, ZoneId b) { return a.Value < b.Value; }))
        {
            partition.PinZone(zone, minimum);
        }
    }

    for (const ZoneId zone : Pinned_)
    {
        if (!std::binary_search(Wanted_.begin(), Wanted_.end(), zone,
                                [](ZoneId a, ZoneId b) { return a.Value < b.Value; }))
        {
            partition.UnpinZone(zone);
        }
    }

    Pinned_.swap(Wanted_);
}
