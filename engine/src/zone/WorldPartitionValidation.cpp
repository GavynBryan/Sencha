#include <zone/WorldPartitionValidation.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>

namespace
{

std::string Hex(uint64_t value)
{
    return std::format("{:016x}", value);
}

bool IsFinitePositiveAabb(const Aabb3d& bounds)
{
    constexpr float kMinimumExtent = 1.0e-4f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(bounds.Min[axis]) || !std::isfinite(bounds.Max[axis])
            || bounds.Max[axis] - bounds.Min[axis] <= kMinimumExtent)
            return false;
    }
    return true;
}

bool IsFinite(Vec3d value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y)
        && std::isfinite(value.Z);
}

bool IsUnit(Vec3d value)
{
    return IsFinite(value) && std::abs(value.SqrMagnitude() - 1.0f) <= 1.0e-3f;
}

// Human-legible reference for a message: the authored name when set, else the
// hex id. Messages are baked at validation time, so a rename that re-runs
// validation re-renders them with the new name.
std::string ZoneLabel(const WorldPartitionManifest& manifest, ZoneId id)
{
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == id)
            return zone.Name.empty() ? Hex(id.Value) : zone.Name;
    }
    return Hex(id.Value);
}

std::string GraphLabel(const WorldPartitionManifest& manifest, GraphId id)
{
    for (const GraphRecord& graph : manifest.Graphs)
    {
        if (graph.Id == id)
            return graph.Name.empty() ? Hex(id.Value) : graph.Name;
    }
    return Hex(id.Value);
}

std::string TransitionLabel(const WorldPartitionManifest& manifest,
                            const TransitionRecord& transition)
{
    if (!transition.Name.empty())
        return transition.Name;
    return std::format("{} -> {}", ZoneLabel(manifest, transition.From),
                       ZoneLabel(manifest, transition.To));
}

void AppendSortedBySourceId(std::vector<ContentRiskRecord>& records,
                            std::vector<ContentRiskRecord> ruleRecords)
{
    std::sort(ruleRecords.begin(), ruleRecords.end(),
              [](const ContentRiskRecord& a, const ContentRiskRecord& b)
              { return a.SourceId < b.SourceId; });
    for (ContentRiskRecord& record : ruleRecords)
        records.push_back(std::move(record));
}

void AppendDuplicateIds(std::vector<ContentRiskRecord>& records,
                        std::vector<uint64_t> ids,
                        ContentRiskSourceKind kind,
                        const char* typeName)
{
    std::sort(ids.begin(), ids.end());
    for (size_t i = 1; i < ids.size(); ++i)
    {
        if (ids[i] != ids[i - 1])
            continue;
        records.push_back({
            .Severity = ContentRiskSeverity::Error,
            .Kind = kind,
            .SourceId = ids[i],
            .RuleId = "partition.id.duplicate",
            .Message = std::format("duplicate {} id {}", typeName, Hex(ids[i])),
        });
        // Skip further copies of the same value: one record per duplicated id.
        while (i + 1 < ids.size() && ids[i + 1] == ids[i])
            ++i;
    }
}

} // namespace

std::vector<ContentRiskRecord>
ValidateWorldPartitionManifest(const WorldPartitionManifest& manifest,
                               const WorldPartitionIndex& index)
{
    std::vector<ContentRiskRecord> records;

    // partition.id.duplicate
    {
        std::vector<uint64_t> ids;
        for (const GraphRecord& graph : manifest.Graphs)
            ids.push_back(graph.Id.Value);
        AppendDuplicateIds(records, std::move(ids), ContentRiskSourceKind::Graph, "graph");

        ids = {};
        for (const ZoneHeader& zone : manifest.Zones)
            ids.push_back(zone.Id.Value);
        AppendDuplicateIds(records, std::move(ids), ContentRiskSourceKind::Zone, "zone");

        ids = {};
        for (const TransitionRecord& transition : manifest.Transitions)
            ids.push_back(transition.Id.Value);
        AppendDuplicateIds(records, std::move(ids), ContentRiskSourceKind::Transition, "transition");
    }

    // partition.zone.graph_missing
    {
        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            const bool known = std::any_of(
                manifest.Graphs.begin(), manifest.Graphs.end(),
                [&](const GraphRecord& graph) { return graph.Id == zone.Graph; });
            if (known)
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.zone.graph_missing",
                .Message = std::format("zone {} references missing graph {}",
                                       ZoneLabel(manifest, zone.Id),
                                       GraphLabel(manifest, zone.Graph)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.graph.streaming_invalid: one record per bad field, so the
    // panel can name each offending value.
    {
        std::vector<ContentRiskRecord> rule;
        for (const GraphRecord& graph : manifest.Graphs)
        {
            const GraphStreamingConfig& streaming = graph.Streaming;
            const auto add = [&](std::string message)
            {
                rule.push_back({
                    .Severity = ContentRiskSeverity::Error,
                    .Kind = ContentRiskSourceKind::Graph,
                    .SourceId = graph.Id.Value,
                    .RuleId = "partition.graph.streaming_invalid",
                    .Message = std::move(message),
                });
            };
            if (streaming.HopCount && *streaming.HopCount < 0)
                add(std::format("graph {} streaming hop count {} is negative",
                                GraphLabel(manifest, graph.Id), *streaming.HopCount));
            if (streaming.Radius && (!std::isfinite(*streaming.Radius) || *streaming.Radius < 0.0))
                add(std::format("graph {} streaming radius {} must be finite and non-negative",
                                GraphLabel(manifest, graph.Id), *streaming.Radius));
            if (streaming.ResidentZoneCap && *streaming.ResidentZoneCap < 1)
                add(std::format("graph {} streaming resident zone cap {} is below 1",
                                GraphLabel(manifest, graph.Id), *streaming.ResidentZoneCap));
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.transition.endpoint_missing
    {
        std::vector<ContentRiskRecord> rule;
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            if (index.ContainsZone(transition.From) && index.ContainsZone(transition.To))
                continue;
            const ZoneId missing =
                index.ContainsZone(transition.From) ? transition.To : transition.From;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Transition,
                .SourceId = transition.Id.Value,
                .RuleId = "partition.transition.endpoint_missing",
                .Message = std::format("transition {} endpoint {} names no zone",
                                       TransitionLabel(manifest, transition),
                                       Hex(missing.Value)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.transition.self_loop
    {
        std::vector<ContentRiskRecord> rule;
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            if (transition.From != transition.To)
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Transition,
                .SourceId = transition.Id.Value,
                .RuleId = "partition.transition.self_loop",
                .Message = std::format("transition {} connects zone {} to itself",
                                       TransitionLabel(manifest, transition),
                                       ZoneLabel(manifest, transition.From)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.transition.unpaired
    {
        std::vector<ContentRiskRecord> rule;
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            if (transition.Flags.OneWay || transition.Topology == TransitionTopology::Teleport)
                continue;
            const bool paired = std::any_of(
                manifest.Transitions.begin(), manifest.Transitions.end(),
                [&](const TransitionRecord& other)
                { return other.From == transition.To && other.To == transition.From; });
            if (paired)
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Warning,
                .Kind = ContentRiskSourceKind::Transition,
                .SourceId = transition.Id.Value,
                .RuleId = "partition.transition.unpaired",
                .Message = std::format("transition {} ({} to {}) has no reverse edge",
                                       TransitionLabel(manifest, transition),
                                       ZoneLabel(manifest, transition.From),
                                       ZoneLabel(manifest, transition.To)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.dock.endpoint_invalid
    {
        std::vector<const DockEndpoint*> endpoints;
        for (const ZoneHeader& zone : manifest.Zones)
            for (const DockEndpoint& endpoint : zone.Docks)
                endpoints.push_back(&endpoint);
        std::sort(endpoints.begin(), endpoints.end(), [](const auto* a, const auto* b)
        {
            if (a->Id.Value != b->Id.Value)
                return a->Id.Value < b->Id.Value;
            return a->Side < b->Side;
        });
        for (std::size_t begin = 0; begin < endpoints.size();)
        {
            std::size_t end = begin + 1;
            while (end < endpoints.size() && endpoints[end]->Id == endpoints[begin]->Id)
                ++end;
            const DockEndpoint& first = *endpoints[begin];
            bool valid = first.Id.IsValid() && end - begin == 2;
            if (valid)
            {
                const DockEndpoint& second = *endpoints[begin + 1];
                valid = first.Side != second.Side
                    && first.OwnerZone == second.OtherZone
                    && first.OtherZone == second.OwnerZone
                    && first.OwnerGraph == second.OtherGraph
                    && first.OtherGraph == second.OwnerGraph;
            }
            for (std::size_t i = begin; i < end && valid; ++i)
            {
                const DockEndpoint& endpoint = *endpoints[i];
                const ZoneHeader* owner = nullptr;
                const ZoneHeader* other = nullptr;
                for (const ZoneHeader& zone : manifest.Zones)
                {
                    if (zone.Id == endpoint.OwnerZone)
                        owner = &zone;
                    if (zone.Id == endpoint.OtherZone)
                        other = &zone;
                }
                valid = owner != nullptr && other != nullptr && owner != other
                    && owner->Graph == endpoint.OwnerGraph
                    && other->Graph == endpoint.OtherGraph
                    && endpoint.HalfExtents.X > 0.0f && endpoint.HalfExtents.Y > 0.0f
                    && IsFinite(endpoint.Origin)
                    && IsFinitePositiveAabb(endpoint.OwnerArmBoundsLocal)
                    && endpoint.OwnerArmBoundsLocal.Max.Z <= 1.0e-4f
                    && IsUnit(endpoint.Normal) && IsUnit(endpoint.Right)
                    && IsUnit(endpoint.Up)
                    && std::abs(endpoint.Normal.Dot(endpoint.Right)) <= 1.0e-3f
                    && std::abs(endpoint.Normal.Dot(endpoint.Up)) <= 1.0e-3f
                    && std::abs(endpoint.Right.Dot(endpoint.Up)) <= 1.0e-3f
                    && endpoint.Directions >= 1u && endpoint.Directions <= 3u;
            }
            if (!valid)
            {
                records.push_back({
                    .Severity = ContentRiskSeverity::Error,
                    .Kind = ContentRiskSourceKind::Dock,
                    .SourceId = first.Id.Value,
                    .RuleId = "partition.dock.endpoint_invalid",
                    .Message = std::format("dock {} does not have two valid reciprocal endpoints",
                                           Hex(first.Id.Value)),
                });
            }
            begin = end;
        }
    }

    // partition.link.endpoint_invalid
    {
        std::vector<const LinkEndpoint*> endpoints;
        for (const ZoneHeader& zone : manifest.Zones)
            for (const LinkEndpoint& endpoint : zone.Links)
                endpoints.push_back(&endpoint);
        std::sort(endpoints.begin(), endpoints.end(), [](const auto* a, const auto* b)
        {
            if (a->Id.Value != b->Id.Value)
                return a->Id.Value < b->Id.Value;
            return a->Side < b->Side;
        });
        for (std::size_t begin = 0; begin < endpoints.size();)
        {
            std::size_t end = begin + 1;
            while (end < endpoints.size() && endpoints[end]->Id == endpoints[begin]->Id)
                ++end;
            const LinkEndpoint& first = *endpoints[begin];
            bool valid = first.Id.IsValid() && end - begin == 2 && first.Kind == 0;
            if (valid)
            {
                const LinkEndpoint& second = *endpoints[begin + 1];
                valid = first.Side != second.Side && first.Kind == second.Kind
                    && first.OwnerZone == second.OtherZone
                    && first.OtherZone == second.OwnerZone
                    && first.Directions >= 1u && first.Directions <= 3u;
            }
            if (!valid)
            {
                records.push_back({
                    .Severity = ContentRiskSeverity::Error,
                    .Kind = ContentRiskSourceKind::Link,
                    .SourceId = first.Id.Value,
                    .RuleId = "partition.link.endpoint_invalid",
                    .Message = std::format("link {} does not have two valid reciprocal endpoints",
                                           Hex(first.Id.Value)),
                });
            }
            begin = end;
        }
    }

    // partition.zone.scene_missing
    {
        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (!zone.SceneRef.empty())
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.zone.scene_missing",
                .Message = std::format("zone {} has no scene reference",
                                       ZoneLabel(manifest, zone.Id)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.zone.bounds_invalid
    {
        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (IsFinitePositiveAabb(zone.Bounds))
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.zone.bounds_invalid",
                .Message = std::format("zone {} bounds are invalid",
                                       ZoneLabel(manifest, zone.Id)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.graph.unreachable, suppressed entirely when no_start_zone fires.
    const bool startZoneKnown =
        manifest.StartZone.IsValid() && index.ContainsZone(manifest.StartZone);
    if (startZoneKnown)
    {
        // A graph with an explicit Radius > 0 override streams by proximity,
        // not authored edges, so its zones are mutually reachable: reaching any
        // one of them reaches them all. Only explicit overrides participate;
        // validation is pure over the manifest and cannot see the inherited
        // base radius in EngineRuntimeConfig.
        std::unordered_set<uint64_t> radiusGraphs;
        for (const GraphRecord& graph : manifest.Graphs)
            if (graph.Streaming.Radius && *graph.Streaming.Radius > 0.0)
                radiusGraphs.insert(graph.Id.Value);

        std::unordered_set<uint64_t> reached;
        std::vector<ZoneId> frontier{ manifest.StartZone };
        reached.insert(manifest.StartZone.Value);
        while (!frontier.empty())
        {
            const ZoneId zone = frontier.back();
            frontier.pop_back();
            for (const ZoneHeader& header : manifest.Zones)
            {
                if (header.Id != zone || !radiusGraphs.contains(header.Graph.Value))
                    continue;
                for (const ZoneHeader& sibling : manifest.Zones)
                    if (sibling.Graph == header.Graph
                        && reached.insert(sibling.Id.Value).second)
                        frontier.push_back(sibling.Id);
            }
            for (const uint32_t edge : index.Outgoing(zone))
            {
                const ZoneId next = manifest.Transitions[edge].To;
                if (reached.insert(next.Value).second)
                    frontier.push_back(next);
            }
            for (const uint32_t edge : index.Incoming(zone))
            {
                const TransitionRecord& transition = manifest.Transitions[edge];
                if (transition.Flags.OneWay)
                    continue;
                if (reached.insert(transition.From.Value).second)
                    frontier.push_back(transition.From);
            }
            const auto endpointOutgoing = [](DockSide side, uint32_t directions)
            {
                return side == DockSide::A ? (directions & 1u) != 0
                                           : (directions & 2u) != 0;
            };
            for (const DockEndpoint& endpoint : index.DocksFrom(zone))
            {
                if (endpointOutgoing(endpoint.Side, endpoint.Directions)
                    && reached.insert(endpoint.OtherZone.Value).second)
                    frontier.push_back(endpoint.OtherZone);
            }
            for (const LinkEndpoint& endpoint : index.LinksFrom(zone))
            {
                if (endpointOutgoing(endpoint.Side, endpoint.Directions)
                    && reached.insert(endpoint.OtherZone.Value).second)
                    frontier.push_back(endpoint.OtherZone);
            }
        }

        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (reached.contains(zone.Id.Value))
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Warning,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.graph.unreachable",
                .Message = std::format("zone {} is not reachable from the start zone",
                                       ZoneLabel(manifest, zone.Id)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }
    else
    {
        records.push_back({
            .Severity = ContentRiskSeverity::Warning,
            .Kind = ContentRiskSourceKind::World,
            .SourceId = 0,
            .RuleId = "partition.world.no_start_zone",
            .Message = "world start zone is not designated or names no zone",
        });
    }

    return records;
}
