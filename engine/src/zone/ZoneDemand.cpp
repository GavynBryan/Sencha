#include <zone/ZoneDemand.h>

#include <algorithm>
#include <utility>

namespace
{

bool ZoneExists(const WorldPartitionManifest& manifest, ZoneId zone)
{
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == zone)
            return true;
    return false;
}

ZoneHopRank* FindRank(std::vector<ZoneHopRank>& ranks, ZoneId zone)
{
    for (ZoneHopRank& rank : ranks)
        if (rank.Zone == zone)
            return &rank;
    return nullptr;
}

double BoundsVolume(const Aabb3d& bounds)
{
    const Vec3d extent = bounds.Extent();
    return static_cast<double>(extent[0]) * static_cast<double>(extent[1])
        * static_cast<double>(extent[2]);
}

} // namespace

ZoneId ResolveFocusZone(const WorldPartitionManifest& manifest, Vec3d position,
                        ZoneId previous)
{
    // Hysteresis: the previous focus wins while the position stays inside it,
    // so a doorway threshold does not flap focus between overlapping bounds.
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == previous && header.Bounds.Contains(position))
            return previous;

    const ZoneHeader* best = nullptr;
    for (const ZoneHeader& header : manifest.Zones)
    {
        if (!header.Bounds.Contains(position))
            continue;
        if (best == nullptr || BoundsVolume(header.Bounds) < BoundsVolume(best->Bounds)
            || (BoundsVolume(header.Bounds) == BoundsVolume(best->Bounds)
                && header.Id.Value < best->Id.Value))
            best = &header;
    }
    return best != nullptr ? best->Id : previous;
}

std::vector<ZoneHopRank> ComputeZoneHopRanks(const WorldPartitionManifest& manifest,
                                             const WorldPartitionIndex& index,
                                             ZoneId focus,
                                             int32_t hopCount)
{
    if (!focus.IsValid() || !ZoneExists(manifest, focus))
        return {};

    std::vector<ZoneHopRank> ranks;
    ranks.push_back(ZoneHopRank{ focus, 0, std::numeric_limits<int32_t>::min() });

    // Outgoing edges only: a two-way door is two edges (the unpaired validation
    // rule keeps it that way), so paired doors are symmetric by construction
    // and a OneWay edge INTO the focus does not preload its source.
    std::vector<ZoneId> frontier{ focus };
    for (int32_t hop = 1; hop <= hopCount; ++hop)
    {
        std::vector<ZoneId> next;
        for (ZoneId zone : frontier)
        {
            for (uint32_t edgeIndex : index.Outgoing(zone))
            {
                const TransitionRecord& edge = manifest.Transitions[edgeIndex];
                if (!ZoneExists(manifest, edge.To))
                    continue;
                if (ZoneHopRank* existing = FindRank(ranks, edge.To))
                {
                    // A zone reachable by several edges at its shortest hop
                    // keeps the highest priority among those discoveries.
                    if (existing->Hop == hop && edge.PreloadPriority > existing->Priority)
                        existing->Priority = edge.PreloadPriority;
                    continue;
                }
                ranks.push_back(ZoneHopRank{ edge.To, hop, edge.PreloadPriority });
                next.push_back(edge.To);
            }
        }
        frontier = std::move(next);
    }

    std::sort(ranks.begin(), ranks.end(),
              [](const ZoneHopRank& a, const ZoneHopRank& b)
              { return a.Zone.Value < b.Zone.Value; });
    return ranks;
}

std::vector<ZoneDemandRecord> ComputeZoneDemand(const WorldPartitionManifest& manifest,
                                                const WorldPartitionIndex& index,
                                                ZoneId focus,
                                                std::span<const ZonePin> pins,
                                                const WorldPartitionStreamingConfig& config)
{
    struct DemandEntry
    {
        ZoneHopRank       Rank;
        ZoneParticipation Desired;
        ZoneDemandSources Sources;
        bool              Pinned = false;
    };

    // The caller decides what "no focus yet" means; the policy does not guess.
    const std::vector<ZoneHopRank> ranks =
        ComputeZoneHopRanks(manifest, index, focus, config.HopCount);
    if (ranks.empty())
        return {};

    std::vector<DemandEntry> entries;
    entries.reserve(ranks.size());
    for (const ZoneHopRank& rank : ranks)
    {
        DemandEntry entry;
        entry.Rank = rank;
        if (rank.Zone == focus)
        {
            entry.Desired = ZoneParticipation{ .Visible = true, .Physics = true,
                                               .Logic = true, .Audio = true };
            entry.Sources.Focus = true;
        }
        else
        {
            entry.Sources.Neighbor = true;
        }
        entries.push_back(entry);
    }

    const auto find = [&](ZoneId zone) -> DemandEntry*
    {
        for (DemandEntry& entry : entries)
            if (entry.Rank.Zone == zone)
                return &entry;
        return nullptr;
    };

    // Pins OR their minimum onto whatever the zone already earned. A pin on a
    // zone the manifest does not contain is ignored: validation owns reporting
    // broken content; the policy stays total.
    for (const ZonePin& pin : pins)
    {
        if (!pin.Zone.IsValid() || !ZoneExists(manifest, pin.Zone))
            continue;
        DemandEntry* existing = find(pin.Zone);
        if (existing == nullptr)
        {
            DemandEntry entry;
            entry.Rank = ZoneHopRank{ pin.Zone, std::numeric_limits<int32_t>::max(),
                                      std::numeric_limits<int32_t>::min() };
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
                      if (entries[a].Rank.Hop != entries[b].Rank.Hop)
                          return entries[a].Rank.Hop > entries[b].Rank.Hop;
                      if (entries[a].Rank.Priority != entries[b].Rank.Priority)
                          return entries[a].Rank.Priority < entries[b].Rank.Priority;
                      return entries[a].Rank.Zone.Value > entries[b].Rank.Zone.Value;
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
              { return a.Rank.Zone.Value < b.Rank.Zone.Value; });

    std::vector<ZoneDemandRecord> records;
    records.reserve(entries.size());
    for (const DemandEntry& entry : entries)
        records.push_back(ZoneDemandRecord{ entry.Rank.Zone, entry.Desired, entry.Sources });
    return records;
}
