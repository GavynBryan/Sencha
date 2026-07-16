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

GraphId GraphOf(const WorldPartitionManifest& manifest, ZoneId zone)
{
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == zone)
            return header.Graph;
    return {};
}

bool TagsPresent(const std::vector<std::string>& required,
                 std::span<const std::string> active)
{
    for (const std::string& tag : required)
    {
        if (std::find(active.begin(), active.end(), tag) == active.end())
            return false;
    }
    return true;
}

bool EndpointAllowsOutgoing(DockSide side, uint32_t directions)
{
    constexpr uint32_t aToB = 1u << 0;
    constexpr uint32_t bToA = 1u << 1;
    return side == DockSide::A ? (directions & aToB) != 0 : (directions & bToA) != 0;
}

void AddReason(std::vector<ZoneDemandReasonRecord>& reasons,
               ZoneDemandReasonRecord reason)
{
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end())
        reasons.push_back(std::move(reason));
}

double BoundsVolume(const Aabb3d& bounds)
{
    const Vec3d extent = bounds.Extent();
    return static_cast<double>(extent[0]) * static_cast<double>(extent[1])
        * static_cast<double>(extent[2]);
}

Vec3d ToEndpointLocal(const DockEndpoint& endpoint, Vec3d point)
{
    const Vec3d delta = point - endpoint.Origin;
    return Vec3d{ delta.Dot(endpoint.Right), delta.Dot(endpoint.Up),
                  delta.Dot(endpoint.Normal) };
}

} // namespace

WorldPartitionStreamingConfig
ResolveGraphStreamingConfig(const WorldPartitionManifest& manifest, ZoneId focus,
                             const WorldPartitionStreamingConfig& base)
{
    WorldPartitionStreamingConfig resolved = base;
    if (!focus.IsValid())
        return resolved;

    GraphId focusGraph;
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == focus)
            focusGraph = header.Graph;

    for (const GraphRecord& graph : manifest.Graphs)
    {
        if (graph.Id != focusGraph)
            continue;
        if (graph.Streaming.HopCount)
            resolved.HopCount = *graph.Streaming.HopCount;
        if (graph.Streaming.Radius)
            resolved.Radius = *graph.Streaming.Radius;
        if (graph.Streaming.ResidentZoneCap)
            resolved.ResidentZoneCap = *graph.Streaming.ResidentZoneCap;
        break;
    }
    return resolved;
}

ZoneContainmentResult ResolveZoneAt(const WorldPartitionManifest& manifest,
                                    Vec3d position, ZoneId preferred)
{
    ZoneContainmentResult result;
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Bounds.Contains(position))
            result.Candidates.push_back(header.Id);
    std::sort(result.Candidates.begin(), result.Candidates.end(),
              [](ZoneId a, ZoneId b) { return a.Value < b.Value; });
    result.Ambiguous = result.Candidates.size() > 1;

    if (std::find(result.Candidates.begin(), result.Candidates.end(), preferred)
        != result.Candidates.end())
    {
        result.Chosen = preferred;
        return result;
    }

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
    if (best != nullptr)
    {
        result.Chosen = best->Id;
        return result;
    }

    // Inside no zone: the nearest bounds wins (ties: smaller volume, then
    // id). Derived bounds hug authored geometry, so a pawn standing on a
    // floor slab or airborne is routinely outside every box; keeping the
    // previous focus would freeze streaming on whatever zone was entered
    // last. Previous survives only when no zone has valid bounds.
    double bestDistanceSq = 0.0;
    double bestVolume = 0.0;
    for (const ZoneHeader& header : manifest.Zones)
    {
        if (!header.Bounds.IsValid())
            continue;
        Vec3d closest;
        for (int axis = 0; axis < 3; ++axis)
            closest[axis] = std::clamp(position[axis], header.Bounds.Min[axis],
                                       header.Bounds.Max[axis]);
        const Vec3d delta = closest - position;
        const double distanceSq = static_cast<double>(delta[0]) * delta[0]
            + static_cast<double>(delta[1]) * delta[1]
            + static_cast<double>(delta[2]) * delta[2];
        const double volume = BoundsVolume(header.Bounds);
        const bool better = best == nullptr || distanceSq < bestDistanceSq
            || (distanceSq == bestDistanceSq
                && (volume < bestVolume
                    || (volume == bestVolume && header.Id.Value < best->Id.Value)));
        if (better)
        {
            best = &header;
            bestDistanceSq = distanceSq;
            bestVolume = volume;
        }
    }
    result.Chosen = best != nullptr ? best->Id : preferred;
    return result;
}

ZoneId ResolveFocusZone(const WorldPartitionManifest& manifest, Vec3d position,
                        ZoneId previous)
{
    return ResolveZoneAt(manifest, position, previous).Chosen;
}

std::vector<ZoneHopRank> ComputeZoneHopRanks(const WorldPartitionManifest& manifest,
                                             const WorldPartitionIndex& index,
                                             ZoneId focus,
                                             int32_t hopCount,
                                             std::span<const std::string> activeTags)
{
    if (!focus.IsValid() || !ZoneExists(manifest, focus))
        return {};

    const GraphId focusGraph = GraphOf(manifest, focus);

    std::vector<ZoneHopRank> ranks;
    ranks.push_back(ZoneHopRank{ focus, 0, std::numeric_limits<int32_t>::min(),
                                  ZoneDemandReason::Focus, focus, 0 });

    // Outgoing endpoints only: bilateral docks and links expose one endpoint
    // per zone and direction masks decide whether it can expand from this side.
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
        const auto consider = [&](ZoneId destination, int32_t priority, int32_t preloadDepth,
                                  const std::vector<std::string>& requiredTags,
                                  uint64_t endpointId)
        {
            if (!ZoneExists(manifest, destination) || !TagsPresent(requiredTags, activeTags))
                return;
            const GraphId currentGraph = GraphOf(manifest, current.Zone);
            const GraphId destinationGraph = GraphOf(manifest, destination);
            const bool crossGraph = currentGraph != destinationGraph;
            if (crossGraph && currentGraph != focusGraph)
                return;
            if (current.Remaining <= 0 && preloadDepth <= 0 && !crossGraph)
                return;

            int32_t farRemaining = std::max(current.Remaining - 1, preloadDepth - 1);
            if (crossGraph)
            {
                WorldPartitionStreamingConfig destinationBase;
                destinationBase.HopCount = hopCount;
                farRemaining = std::max(
                    0, ResolveGraphStreamingConfig(manifest, destination, destinationBase).HopCount);
            }
            const int32_t farHop = current.Hop + 1;

            if (ZoneHopRank* existing = FindRank(ranks, destination))
            {
                if (existing->Hop == farHop && priority > existing->Priority)
                    existing->Priority = priority;
                if (int32_t* known = remainingOf(destination);
                    known != nullptr && farRemaining > *known)
                {
                    *known = farRemaining;
                    queue.push_back(Frontier{ destination, existing->Hop, farRemaining });
                }
                return;
            }
            ranks.push_back(ZoneHopRank{
                destination, farHop, priority,
                crossGraph ? ZoneDemandReason::CrossGraphEntry
                           : ZoneDemandReason::SameGraphHop,
                current.Zone, endpointId });
            bestRemaining.push_back({ destination.Value, farRemaining });
            queue.push_back(Frontier{ destination, farHop, farRemaining });
        };

        for (const DockEndpoint& endpoint : index.DocksFrom(current.Zone))
            if (EndpointAllowsOutgoing(endpoint.Side, endpoint.Directions))
                consider(endpoint.OtherZone, endpoint.PreloadPriority, endpoint.PreloadDepth,
                         endpoint.RequiredTags, endpoint.Id.Value);
        for (const LinkEndpoint& endpoint : index.LinksFrom(current.Zone))
            if (EndpointAllowsOutgoing(endpoint.Side, endpoint.Directions))
                consider(endpoint.OtherZone, endpoint.PreloadPriority, endpoint.PreloadDepth,
                         endpoint.RequiredTags, endpoint.Id.Value);
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
        std::vector<ZoneDemandReasonRecord> Reasons;
        bool              Pinned = false;
    };

    // The caller decides what "no focus yet" means; the policy does not guess.
    const WorldPartitionStreamingConfig focusConfig =
        ResolveGraphStreamingConfig(manifest, focus, config);
    const std::vector<ZoneHopRank> ranks =
        ComputeZoneHopRanks(manifest, index, focus, focusConfig.HopCount, activeTags);
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
            AddReason(entry.Reasons, { ZoneDemandReason::Focus, focus, 0, 0, {} });
        }
        else
        {
            entry.Desired = preload;
            entry.Sources.Neighbor = true;
            entry.Sources.SameGraphHop = rank.Reason == ZoneDemandReason::SameGraphHop;
            entry.Sources.CrossGraphEntry = rank.Reason == ZoneDemandReason::CrossGraphEntry;
            AddReason(entry.Reasons, { rank.Reason, rank.SourceZone,
                                       rank.SourceEndpoint, rank.Hop, {} });
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

    // Being inside an outgoing dock arm is a predictive ordering reason in
    // addition to ordinary graph adjacency. It never creates topology.
    if (focusPosition != nullptr)
    {
        for (const DockEndpoint& endpoint : index.DocksFrom(focus))
        {
            if (!EndpointAllowsOutgoing(endpoint.Side, endpoint.Directions)
                || !TagsPresent(endpoint.RequiredTags, activeTags)
                || !endpoint.OwnerArmBoundsLocal.Contains(
                    ToEndpointLocal(endpoint, *focusPosition)))
                continue;
            if (DemandEntry* destination = find(endpoint.OtherZone))
            {
                destination->Sources.DockApproach = true;
                AddReason(destination->Reasons,
                          { ZoneDemandReason::DockApproach, focus,
                            endpoint.Id.Value, 1, {} });
            }
        }
    }

    // Proximity demand: zones whose bounds' closest point lies within Radius
    // of the focus position (point-to-box, not center-to-center: a huge field
    // cell whose edge is near counts as near). Graph neighbors are always
    // preferred by eviction, so spatial-only entries rank one hop past the
    // horizon with nearer-survives-longer priority; the quantization makes
    // priority ties exact and deterministic.
    struct SpatialSeed
    {
        GraphId Graph;
        ZoneId Zone;
        Vec3d Position;
        WorldPartitionStreamingConfig Config;
    };
    std::vector<SpatialSeed> spatialSeeds;
    if (focusPosition != nullptr)
        spatialSeeds.push_back({ GraphOf(manifest, focus), focus, *focusPosition, focusConfig });
    if (focusPosition != nullptr)
    {
        for (const ZoneHopRank& rank : ranks)
        {
            const GraphId graph = GraphOf(manifest, rank.Zone);
            if (std::any_of(spatialSeeds.begin(), spatialSeeds.end(),
                            [&](const SpatialSeed& seed) { return seed.Graph == graph; }))
                continue;
            for (const ZoneHeader& header : manifest.Zones)
            {
                if (header.Id == rank.Zone && header.Bounds.IsValid())
                {
                    spatialSeeds.push_back({ graph, rank.Zone, header.Bounds.Center(),
                        ResolveGraphStreamingConfig(manifest, rank.Zone, config) });
                    break;
                }
            }
        }
    }

    for (const SpatialSeed& seed : spatialSeeds)
    {
        if (seed.Config.Radius <= 0.0)
            continue;
        for (const ZoneHeader& header : manifest.Zones)
        {
            if (header.Id == seed.Zone || header.Graph != seed.Graph
                || !header.Bounds.IsValid())
                continue;
            const Vec3d closest{
                std::clamp(seed.Position[0], header.Bounds.Min[0], header.Bounds.Max[0]),
                std::clamp(seed.Position[1], header.Bounds.Min[1], header.Bounds.Max[1]),
                std::clamp(seed.Position[2], header.Bounds.Min[2], header.Bounds.Max[2])
            };
            const Vec3d delta = closest - seed.Position;
            const double distanceSq = static_cast<double>(delta[0]) * delta[0]
                + static_cast<double>(delta[1]) * delta[1]
                + static_cast<double>(delta[2]) * delta[2];
            if (distanceSq > seed.Config.Radius * seed.Config.Radius)
                continue;
            if (DemandEntry* existing = find(header.Id))
            {
                existing->Sources.Spatial = true;
                existing->Sources.SpatialRadius = true;
                existing->Desired.Visible |= preload.Visible;
                existing->Desired.Physics |= preload.Physics;
                AddReason(existing->Reasons,
                          { ZoneDemandReason::SpatialRadius, seed.Zone, 0,
                            existing->Rank.Hop, std::sqrt(distanceSq) });
                continue;
            }
            DemandEntry entry;
            entry.Rank = ZoneHopRank{
                header.Id, seed.Config.HopCount + 1,
                -static_cast<int32_t>(std::lround(std::sqrt(distanceSq) * 100.0)),
                ZoneDemandReason::SpatialRadius, seed.Zone, 0
            };
            entry.Desired = preload;
            entry.Sources.Spatial = true;
            entry.Sources.SpatialRadius = true;
            AddReason(entry.Reasons,
                      { ZoneDemandReason::SpatialRadius, seed.Zone, 0,
                        entry.Rank.Hop, std::sqrt(distanceSq) });
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
            entry.Rank = ZoneHopRank{
                pin.Zone, std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::min(),
                ZoneDemandReason::ExplicitPin, pin.Zone, 0 };
            entry.Desired = pin.Minimum;
            entry.Sources.Pinned = true;
            entry.Sources.ExplicitPin = true;
            AddReason(entry.Reasons,
                      { ZoneDemandReason::ExplicitPin, pin.Zone, 0,
                        std::numeric_limits<int32_t>::max(), {} });
            entry.Pinned = true;
            entries.push_back(entry);
            continue;
        }
        existing->Desired.Visible |= pin.Minimum.Visible;
        existing->Desired.Physics |= pin.Minimum.Physics;
        existing->Desired.Logic |= pin.Minimum.Logic;
        existing->Desired.Audio |= pin.Minimum.Audio;
        existing->Sources.Pinned = true;
        existing->Sources.ExplicitPin = true;
        AddReason(existing->Reasons,
                  { ZoneDemandReason::ExplicitPin, pin.Zone, 0,
                    std::numeric_limits<int32_t>::max(), {} });
        existing->Pinned = true;
    }

    // Cap: evict non-focus non-pinned zones (hop descending, then priority
    // ascending, then id descending) until the cap is met. Focus plus pins may
    // exceed the cap: pins are explicit demands, silently dropping one would
    // be a policy lie.
    std::vector<bool> evicted(entries.size(), false);
    std::vector<GraphId> demandedGraphs;
    for (const DemandEntry& entry : entries)
    {
        const GraphId graph = GraphOf(manifest, entry.Rank.Zone);
        if (std::find(demandedGraphs.begin(), demandedGraphs.end(), graph) == demandedGraphs.end())
            demandedGraphs.push_back(graph);
    }
    for (GraphId graph : demandedGraphs)
    {
        ZoneId graphSeed;
        std::size_t remaining = 0;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (GraphOf(manifest, entries[i].Rank.Zone) != graph)
                continue;
            if (!graphSeed.IsValid())
                graphSeed = entries[i].Rank.Zone;
            ++remaining;
        }
        const int32_t cap = ResolveGraphStreamingConfig(manifest, graphSeed, config)
                                .ResidentZoneCap;
        if (remaining <= static_cast<std::size_t>(cap))
            continue;
        std::vector<size_t> evictable;
        for (size_t i = 0; i < entries.size(); ++i)
            if (GraphOf(manifest, entries[i].Rank.Zone) == graph
                && !entries[i].Sources.Focus && !entries[i].Pinned)
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
        for (size_t candidate : evictable)
        {
            if (remaining <= static_cast<size_t>(cap))
                break;
            evicted[candidate] = true;
            --remaining;
        }
    }
    std::vector<DemandEntry> kept;
    kept.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
        if (!evicted[i])
            kept.push_back(entries[i]);
    entries = std::move(kept);

    std::sort(entries.begin(), entries.end(),
              [](const DemandEntry& a, const DemandEntry& b)
              { return a.Rank.Zone.Value < b.Rank.Zone.Value; });

    std::vector<ZoneDemandRecord> records;
    records.reserve(entries.size());
    for (const DemandEntry& entry : entries)
        records.push_back(ZoneDemandRecord{
            entry.Rank.Zone, entry.Desired, entry.Sources, entry.Reasons });
    return records;
}
