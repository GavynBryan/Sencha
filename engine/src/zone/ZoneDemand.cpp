#include <zone/ZoneDemand.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
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

// Existence and graph in one pass, for callers that want both. Asking through
// ZoneExists and GraphOf separately walks the zone list twice for one answer,
// which the hop-rank BFS does once per edge it considers.
const ZoneHeader* FindZoneHeader(const WorldPartitionManifest& manifest, ZoneId zone)
{
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == zone)
            return &header;
    return nullptr;
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
                                             int32_t hopCount)
{
    if (!focus.IsValid() || !ZoneExists(manifest, focus))
        return {};

    const GraphId focusGraph = GraphOf(manifest, focus);

    std::vector<ZoneHopRank> ranks;
    ranks.push_back(ZoneHopRank{ focus, 0, 0.0,
                                  ZoneDemandReason::Focus, focus, 0 });

    // Outgoing endpoints only: bilateral docks and links expose one endpoint
    // per zone and direction masks decide whether it can expand from this side.
    // A cross-graph connection seeds the destination even at the current
    // graph's hop boundary. The destination Graph then supplies its own hop
    // policy. Expansion never walks onward into a third Graph in the same pass.
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
        // The frontier zone's graph is the same for every edge leaving it, and
        // a high-degree zone leaves a lot of them; resolving it per edge made
        // the BFS cost the zone count times the degree.
        const GraphId currentGraph = GraphOf(manifest, current.Zone);
        const auto consider = [&](ZoneId destination, uint64_t endpointId)
        {
            const ZoneHeader* destinationHeader = FindZoneHeader(manifest, destination);
            if (destinationHeader == nullptr)
                return;
            const GraphId destinationGraph = destinationHeader->Graph;
            const bool crossGraph = currentGraph != destinationGraph;
            if (crossGraph && currentGraph != focusGraph)
                return;
            if (current.Remaining <= 0 && !crossGraph)
                return;

            int32_t farRemaining = std::max(current.Remaining - 1, 0);
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
                if (int32_t* known = remainingOf(destination);
                    known != nullptr && farRemaining > *known)
                {
                    *known = farRemaining;
                    queue.push_back(Frontier{ destination, existing->Hop, farRemaining });
                }
                return;
            }
            ranks.push_back(ZoneHopRank{
                destination, farHop, 0.0,
                crossGraph ? ZoneDemandReason::CrossGraphEntry
                           : ZoneDemandReason::SameGraphHop,
                current.Zone, endpointId });
            bestRemaining.push_back({ destination.Value, farRemaining });
            queue.push_back(Frontier{ destination, farHop, farRemaining });
        };

        for (const DockEndpoint& endpoint : index.DocksFrom(current.Zone))
            if (EndpointAllowsOutgoing(endpoint.Side, endpoint.Directions))
                consider(endpoint.OtherZone, endpoint.Id.Value);
        for (const LinkEndpoint& endpoint : index.LinksFrom(current.Zone))
            if (EndpointAllowsOutgoing(endpoint.Side, endpoint.Directions))
                consider(endpoint.OtherZone, endpoint.Id.Value);
    }

    std::sort(ranks.begin(), ranks.end(),
              [](const ZoneHopRank& a, const ZoneHopRank& b)
              { return a.Zone.Value < b.Zone.Value; });
    return ranks;
}

namespace
{
    struct DemandEntry
    {
        ZoneHopRank       Rank;
        ZoneParticipation Desired;
        std::vector<ZoneDemandReasonRecord> Reasons;
        bool              Pinned = false;
        // Some source is standing here. Full participation and immunity from
        // eviction follow from it, and with several sources it is no longer the
        // same question as "is this zone the focus".
        bool              Focused = false;
    };

    // Everything one source asks for, before pins, before the cap, and before
    // any other source is considered. Empty for a focus the manifest does not
    // have, which is how a source that has not resolved yet contributes nothing
    // rather than contributing a guess.
    std::vector<DemandEntry> AccumulateSourceDemand(
        const WorldPartitionManifest& manifest,
        const WorldPartitionIndex& index,
        ZoneId focus,
        const Vec3d* focusPosition,
        const WorldPartitionStreamingConfig& config)
    {
    // The caller decides what "no focus yet" means; the policy does not guess.
    const WorldPartitionStreamingConfig focusConfig =
        ResolveGraphStreamingConfig(manifest, focus, config);
    const std::vector<ZoneHopRank> ranks =
        ComputeZoneHopRanks(manifest, index, focus, focusConfig.HopCount);
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
            entry.Focused = true;
            AddReason(entry.Reasons, { ZoneDemandReason::Focus, focus, 0, 0, {} });
        }
        else
        {
            entry.Desired = preload;
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
                std::sqrt(distanceSq),
                ZoneDemandReason::SpatialRadius, seed.Zone, 0
            };
            entry.Desired = preload;
            AddReason(entry.Reasons,
                      { ZoneDemandReason::SpatialRadius, seed.Zone, 0,
                        entry.Rank.Hop, std::sqrt(distanceSq) });
            entries.push_back(entry);
        }
    }

    return entries;
    }

    // Folds one source's demand into the set built so far. A zone several
    // sources want keeps the smallest hop any of them gave it -- and with it the
    // reason that hop came from, so the record still names a real edge -- while
    // participation unions and reasons accumulate.
    void MergeSourceDemand(std::vector<DemandEntry>& into,
                           const std::vector<DemandEntry>& from)
    {
        for (const DemandEntry& add : from)
        {
            const auto held = std::find_if(
                into.begin(), into.end(), [&](const DemandEntry& existing)
                { return existing.Rank.Zone == add.Rank.Zone; });
            if (held == into.end())
            {
                into.push_back(add);
                continue;
            }

            // Strictly nearer wins, so an equal hop keeps the earlier source's
            // reason and the merge does not depend on connection order.
            if (add.Rank.Hop < held->Rank.Hop
                || (add.Rank.Hop == held->Rank.Hop && add.Rank.Cost < held->Rank.Cost))
            {
                const bool wasFocused = held->Focused;
                std::vector<ZoneDemandReasonRecord> reasons = std::move(held->Reasons);
                const ZoneParticipation desired = held->Desired;
                held->Rank = add.Rank;
                held->Reasons = std::move(reasons);
                held->Desired = desired;
                held->Focused = wasFocused;
            }

            held->Desired.Visible |= add.Desired.Visible;
            held->Desired.Physics |= add.Desired.Physics;
            held->Desired.Logic   |= add.Desired.Logic;
            held->Desired.Audio   |= add.Desired.Audio;
            held->Focused = held->Focused || add.Focused;
            for (const ZoneDemandReasonRecord& reason : add.Reasons)
                AddReason(held->Reasons, reason);
        }
    }
}

std::vector<ZoneDemandRecord> ComputeZoneDemand(const WorldPartitionManifest& manifest,
                                                const WorldPartitionIndex& index,
                                                std::span<const ZoneFocusSource> sources,
                                                std::span<const ZonePin> pins,
                                                const WorldPartitionStreamingConfig& config)
{
    std::vector<DemandEntry> entries;
    bool anySourceResolved = false;
    for (const ZoneFocusSource& source : sources)
    {
        const Vec3d position = source.Position.value_or(Vec3d{});
        std::vector<DemandEntry> mine = AccumulateSourceDemand(
            manifest, index, source.Focus,
            source.Position.has_value() ? &position : nullptr, config);
        if (mine.empty())
            continue;
        anySourceResolved = true;

        // The room this source is part way into counts as somewhere it is,
        // not somewhere it can see. It is already in `mine` as a neighbour --
        // a dock is a graph edge -- so this promotes it rather than adding it.
        if (source.Entering.IsValid() && source.Entering != source.Focus)
        {
            for (DemandEntry& entry : mine)
            {
                if (entry.Rank.Zone != source.Entering)
                    continue;
                entry.Desired = ZoneParticipation{ .Visible = true,
                                                   .Physics = true,
                                                   .Logic = true,
                                                   .Audio = true };
                entry.Focused = true;
                break;
            }
        }
        if (entries.empty())
            entries = std::move(mine);
        else
            MergeSourceDemand(entries, mine);
    }

    // No source resolved to anything the manifest has. Pins are deliberately not
    // applied here: a world with no valid focus has nothing to hold zones open
    // around, and answering with pins alone would report residency the runtime
    // is not going to establish.
    if (!anySourceResolved)
        return {};

    // Pins OR their minimum onto whatever the zone already earned. A pin on a
    // zone the manifest does not contain is ignored: validation owns reporting
    // broken content; the policy stays total.
    for (const ZonePin& pin : pins)
    {
        if (!pin.Zone.IsValid() || !ZoneExists(manifest, pin.Zone))
            continue;
        const auto found = std::find_if(
            entries.begin(), entries.end(), [&](const DemandEntry& entry)
            { return entry.Rank.Zone == pin.Zone; });
        DemandEntry* existing = found == entries.end() ? nullptr : &*found;
        if (existing == nullptr)
        {
            DemandEntry entry;
            entry.Rank = ZoneHopRank{
                pin.Zone, std::numeric_limits<int32_t>::max(),
                0.0,
                ZoneDemandReason::ExplicitPin, pin.Zone, 0 };
            entry.Desired = pin.Minimum;
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
        AddReason(existing->Reasons,
                  { ZoneDemandReason::ExplicitPin, pin.Zone, 0,
                    std::numeric_limits<int32_t>::max(), {} });
        existing->Pinned = true;
    }

    // Cap: evict non-focus non-pinned zones (hop descending, then derived cost
    // descending, then id descending) until the cap is met. Focus plus pins may
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
                && !entries[i].Focused && !entries[i].Pinned)
                evictable.push_back(i);
        std::sort(evictable.begin(), evictable.end(),
                  [&](size_t a, size_t b)
                  {
                      if (entries[a].Rank.Hop != entries[b].Rank.Hop)
                          return entries[a].Rank.Hop > entries[b].Rank.Hop;
                      if (entries[a].Rank.Cost != entries[b].Rank.Cost)
                          return entries[a].Rank.Cost > entries[b].Rank.Cost;
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
            entry.Rank.Zone, entry.Desired, entry.Reasons });
    return records;
}

std::vector<ZoneDemandRecord> ComputeZoneDemand(const WorldPartitionManifest& manifest,
                                                const WorldPartitionIndex& index,
                                                ZoneId focus,
                                                std::span<const ZonePin> pins,
                                                const WorldPartitionStreamingConfig& config,
                                                const Vec3d* focusPosition)
{
    // Kept because single focus is what single-player, the editor preview, and
    // every existing test mean, and spelling it as a one-element array at each
    // of those call sites would be ceremony rather than clarity. It forwards
    // rather than duplicating, so the two can never answer differently.
    const ZoneFocusSource source{
        .Source = kPrimaryFocusSource,
        .Focus = focus,
        .Position = focusPosition == nullptr ? std::optional<Vec3d>{}
                                             : std::optional<Vec3d>{ *focusPosition },
        .Entering = {},
    };
    return ComputeZoneDemand(manifest, index, std::span(&source, 1), pins, config);
}

bool IsDemandedFor(const ZoneDemandRecord& record, ZoneDemandReason reason)
{
    return std::any_of(record.Reasons.begin(), record.Reasons.end(),
                       [reason](const ZoneDemandReasonRecord& entry)
                       { return entry.Reason == reason; });
}

std::string DescribeZoneDemandReasons(const ZoneDemandRecord& record)
{
    // Order lives here rather than in Reasons, which is in accumulation order
    // and can hold one kind several times.
    static constexpr std::pair<ZoneDemandReason, std::string_view> kLabels[] = {
        { ZoneDemandReason::Focus,           "focus" },
        { ZoneDemandReason::SameGraphHop,    "graph hop" },
        { ZoneDemandReason::CrossGraphEntry, "cross-graph entry" },
        { ZoneDemandReason::SpatialRadius,   "radius" },
        { ZoneDemandReason::ExplicitPin,     "pin" },
        { ZoneDemandReason::Gameplay,        "gameplay" },
        { ZoneDemandReason::TraversalGrace,  "traversal grace" },
        { ZoneDemandReason::Linger,          "linger" },
    };

    std::string text;
    for (const auto& [reason, label] : kLabels)
    {
        if (!IsDemandedFor(record, reason))
            continue;
        if (!text.empty())
            text += "+";
        text += label;
    }
    return text;
}
