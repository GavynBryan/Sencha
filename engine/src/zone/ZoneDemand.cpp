#include <zone/ZoneDemand.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace
{

struct DemandEntry
{
    ZoneId            Zone;
    int32_t           Hop = 0;
    // Highest PreloadPriority among the transition edges that discovered the
    // zone at its shortest hop; eviction tiebreaker only.
    int32_t           Priority = std::numeric_limits<int32_t>::min();
    ZoneParticipation Desired;
    ZoneDemandSources Sources;
    bool              Pinned = false;
};

DemandEntry* Find(std::vector<DemandEntry>& entries, ZoneId zone)
{
    for (DemandEntry& entry : entries)
        if (entry.Zone == zone)
            return &entry;
    return nullptr;
}

} // namespace

std::vector<ZoneDemandRecord> ComputeZoneDemand(const WorldPartitionManifest& manifest,
                                                const WorldPartitionIndex& index,
                                                ZoneId focus,
                                                std::span<const ZonePin> pins,
                                                const WorldPartitionStreamingConfig& config)
{
    const auto zoneExists = [&](ZoneId zone)
    {
        for (const ZoneHeader& header : manifest.Zones)
            if (header.Id == zone)
                return true;
        return false;
    };

    // The caller decides what "no focus yet" means; the policy does not guess.
    if (!focus.IsValid() || !zoneExists(focus))
        return {};

    std::vector<DemandEntry> entries;
    {
        DemandEntry entry;
        entry.Zone = focus;
        entry.Desired = ZoneParticipation{ .Visible = true, .Physics = true,
                                           .Logic = true, .Audio = true };
        entry.Sources.Focus = true;
        entries.push_back(entry);
    }

    // BFS over outgoing edges only: a two-way door is two edges (the unpaired
    // validation rule keeps it that way), so paired doors are symmetric by
    // construction and a OneWay edge INTO the focus does not preload its source.
    std::vector<ZoneId> frontier{ focus };
    for (int32_t hop = 1; hop <= config.HopCount; ++hop)
    {
        std::vector<ZoneId> next;
        for (ZoneId zone : frontier)
        {
            for (uint32_t edgeIndex : index.Outgoing(zone))
            {
                const TransitionRecord& edge = manifest.Transitions[edgeIndex];
                if (!zoneExists(edge.To))
                    continue;
                if (DemandEntry* existing = Find(entries, edge.To))
                {
                    // A zone reachable by several edges at its shortest hop
                    // keeps the highest priority among those discoveries.
                    if (existing->Hop == hop && edge.PreloadPriority > existing->Priority)
                        existing->Priority = edge.PreloadPriority;
                    continue;
                }
                DemandEntry entry;
                entry.Zone = edge.To;
                entry.Hop = hop;
                entry.Priority = edge.PreloadPriority;
                entry.Sources.Neighbor = true;
                entries.push_back(entry);
                next.push_back(edge.To);
            }
        }
        frontier = std::move(next);
    }

    // Pins OR their minimum onto whatever the zone already earned. A pin on a
    // zone the manifest does not contain is ignored: validation owns reporting
    // broken content; the policy stays total.
    for (const ZonePin& pin : pins)
    {
        if (!pin.Zone.IsValid() || !zoneExists(pin.Zone))
            continue;
        DemandEntry* existing = Find(entries, pin.Zone);
        if (existing == nullptr)
        {
            DemandEntry entry;
            entry.Zone = pin.Zone;
            entry.Hop = std::numeric_limits<int32_t>::max();
            entry.Desired = pin.Minimum;
            entry.Sources.Pinned = true;
            entry.Pinned = true;
            entries.push_back(entry);
            continue;
        }
        existing->Desired.Visible |= pin.Minimum.Visible;
        existing->Desired.Physics |= pin.Minimum.Physics;
        existing->Desired.Logic |= pin.Minimum.Logic;
        existing->Desired.Audio |= pin.Minimum.Audio;
        existing->Sources.Pinned = true;
        existing->Pinned = true;
    }

    // Cap: evict non-focus non-pinned zones (hop descending, then priority
    // ascending, then id descending) until the cap is met. Focus plus pins may
    // exceed the cap: pins are explicit demands, silently dropping one would
    // be a policy lie.
    if (entries.size() > static_cast<size_t>(config.ResidentZoneCap))
    {
        std::vector<size_t> evictable;
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].Sources.Focus && !entries[i].Pinned)
                evictable.push_back(i);
        std::sort(evictable.begin(), evictable.end(),
                  [&](size_t a, size_t b)
                  {
                      if (entries[a].Hop != entries[b].Hop)
                          return entries[a].Hop > entries[b].Hop;
                      if (entries[a].Priority != entries[b].Priority)
                          return entries[a].Priority < entries[b].Priority;
                      return entries[a].Zone.Value > entries[b].Zone.Value;
                  });
        std::vector<bool> evicted(entries.size(), false);
        size_t remaining = entries.size();
        for (size_t candidate : evictable)
        {
            if (remaining <= static_cast<size_t>(config.ResidentZoneCap))
                break;
            evicted[candidate] = true;
            --remaining;
        }
        std::vector<DemandEntry> kept;
        kept.reserve(remaining);
        for (size_t i = 0; i < entries.size(); ++i)
            if (!evicted[i])
                kept.push_back(entries[i]);
        entries = std::move(kept);
    }

    std::sort(entries.begin(), entries.end(),
              [](const DemandEntry& a, const DemandEntry& b)
              { return a.Zone.Value < b.Zone.Value; });

    std::vector<ZoneDemandRecord> records;
    records.reserve(entries.size());
    for (const DemandEntry& entry : entries)
        records.push_back(ZoneDemandRecord{ entry.Zone, entry.Desired, entry.Sources });
    return records;
}
