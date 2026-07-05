#include <zone/ZoneDemand.h>

#include <algorithm>
#include <cmath>
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

WorldPartitionStreamingConfig
ResolveRegionStreamingConfig(const WorldPartitionManifest& manifest, ZoneId focus,
                             const WorldPartitionStreamingConfig& base)
{
    WorldPartitionStreamingConfig resolved = base;
    if (!focus.IsValid())
        return resolved;

    RegionId focusRegion;
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == focus)
            focusRegion = header.Region;

    for (const RegionRecord& region : manifest.Regions)
    {
        if (region.Id != focusRegion)
            continue;
        if (region.Streaming.HopCount)
            resolved.HopCount = *region.Streaming.HopCount;
        if (region.Streaming.Radius)
            resolved.Radius = *region.Streaming.Radius;
        if (region.Streaming.ResidentZoneCap)
            resolved.ResidentZoneCap = *region.Streaming.ResidentZoneCap;
        break;
    }
    return resolved;
}

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
                                             int32_t hopCount,
                                             std::span<const std::string> activeTags)
{
    const auto gateOpen = [&](const TransitionRecord& edge)
    {
        for (const std::string& required : edge.RequiredTags)
        {
            bool present = false;
            for (const std::string& active : activeTags)
                if (active == required)
                {
                    present = true;
                    break;
                }
            if (!present)
                return false;
        }
        return true;
    };

    if (!focus.IsValid() || !ZoneExists(manifest, focus))
        return {};

    std::vector<ZoneHopRank> ranks;
    ranks.push_back(ZoneHopRank{ focus, 0, std::numeric_limits<int32_t>::min() });

    // Outgoing edges only: a two-way door is two edges (the unpaired validation
    // rule keeps it that way), so paired doors are symmetric by construction
    // and a OneWay edge INTO the focus does not preload its source.
    //
    // The traversal carries a remaining hop budget instead of running level by
    // level: an edge may be crossed while budget remains OR when it carries an
    // authored PreloadDepth, and the far side continues with
    // max(budget - 1, PreloadDepth - 1), so one critical corridor preloads
    // deeper than the global horizon. Hop values stay true BFS distances. A
    // zone re-reached with a larger budget re-expands (FIFO order keeps the
    // sequence deterministic).
    struct Frontier
    {
        ZoneId  Zone;
        int32_t Hop = 0;
        int32_t Remaining = 0;
    };
    std::vector<Frontier> queue{ Frontier{ focus, 0, hopCount } };
    std::vector<std::pair<uint64_t, int32_t>> bestRemaining{ { focus.Value, hopCount } };
    const auto remainingOf = [&](ZoneId zone) -> int32_t*
    {
        for (auto& [id, remaining] : bestRemaining)
            if (id == zone.Value)
                return &remaining;
        return nullptr;
    };

    for (size_t head = 0; head < queue.size(); ++head)
    {
        const Frontier current = queue[head];
        for (uint32_t edgeIndex : index.Outgoing(current.Zone))
        {
            const TransitionRecord& edge = manifest.Transitions[edgeIndex];
            if (!ZoneExists(manifest, edge.To) || !gateOpen(edge))
                continue;
            if (current.Remaining <= 0 && edge.PreloadDepth <= 0)
                continue;
            const int32_t farRemaining =
                std::max(current.Remaining - 1, edge.PreloadDepth - 1);
            const int32_t farHop = current.Hop + 1;

            if (ZoneHopRank* existing = FindRank(ranks, edge.To))
            {
                // A zone reachable by several edges at its shortest hop keeps
                // the highest priority among those discoveries.
                if (existing->Hop == farHop && edge.PreloadPriority > existing->Priority)
                    existing->Priority = edge.PreloadPriority;
                if (int32_t* known = remainingOf(edge.To);
                    known != nullptr && farRemaining > *known)
                {
                    *known = farRemaining;
                    queue.push_back(Frontier{ edge.To, existing->Hop, farRemaining });
                }
                continue;
            }
            ranks.push_back(ZoneHopRank{ edge.To, farHop, edge.PreloadPriority });
            bestRemaining.push_back({ edge.To.Value, farRemaining });
            queue.push_back(Frontier{ edge.To, farHop, farRemaining });
        }
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
                                                const WorldPartitionStreamingConfig& config,
                                                const Vec3d* focusPosition,
                                                std::span<const std::string> activeTags)
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
        ComputeZoneHopRanks(manifest, index, focus, config.HopCount, activeTags);
    if (ranks.empty())
        return {};

    const ZoneParticipation preload{ .Visible = config.NeighborVisible,
                                     .Physics = config.NeighborPhysics };

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
            entry.Desired = preload;
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

    // Proximity demand: zones whose bounds' closest point lies within Radius
    // of the focus position (point-to-box, not center-to-center: a huge field
    // cell whose edge is near counts as near). Graph neighbors are always
    // preferred by eviction, so spatial-only entries rank one hop past the
    // horizon with nearer-survives-longer priority; the quantization makes
    // priority ties exact and deterministic.
    if (config.Radius > 0.0 && focusPosition != nullptr)
    {
        for (const ZoneHeader& header : manifest.Zones)
        {
            if (header.Id == focus || !header.Bounds.IsValid())
                continue;
            const Vec3d closest{
                std::clamp((*focusPosition)[0], header.Bounds.Min[0], header.Bounds.Max[0]),
                std::clamp((*focusPosition)[1], header.Bounds.Min[1], header.Bounds.Max[1]),
                std::clamp((*focusPosition)[2], header.Bounds.Min[2], header.Bounds.Max[2])
            };
            const Vec3d delta = closest - *focusPosition;
            const double distanceSq = static_cast<double>(delta[0]) * delta[0]
                + static_cast<double>(delta[1]) * delta[1]
                + static_cast<double>(delta[2]) * delta[2];
            if (distanceSq > config.Radius * config.Radius)
                continue;
            if (DemandEntry* existing = find(header.Id))
            {
                existing->Sources.Spatial = true;
                existing->Desired.Visible |= preload.Visible;
                existing->Desired.Physics |= preload.Physics;
                continue;
            }
            DemandEntry entry;
            entry.Rank = ZoneHopRank{
                header.Id, config.HopCount + 1,
                -static_cast<int32_t>(std::lround(std::sqrt(distanceSq) * 100.0))
            };
            entry.Desired = preload;
            entry.Sources.Spatial = true;
            entries.push_back(entry);
        }
    }

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
